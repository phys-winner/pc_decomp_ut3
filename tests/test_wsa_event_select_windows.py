"""Native WSAEventSelect/Enum oracle. Own loopback UDP/TCP resources only."""
import ctypes as c
import json
import platform
import socket
import struct
from pathlib import Path

SENTINEL = 0x5A5A5A5A
class NetworkEvents(c.Structure):
    _fields_ = [("bits", c.c_int32), ("errors", c.c_int32 * 10)]

class Oracle:
    def __init__(self):
        self.w = c.WinDLL("ws2_32", use_last_error=True)
        self.k = c.WinDLL("kernel32", use_last_error=True)
        self.sockets, self.events = [], []
        for name, restype, args in [
            ("WSAStartup",c.c_int,[c.c_uint16,c.c_void_p]),("WSACleanup",c.c_int,[]),
            ("WSAGetLastError",c.c_int,[]),("WSASetLastError",None,[c.c_int]),
            ("socket",c.c_size_t,[c.c_int,c.c_int,c.c_int]),("closesocket",c.c_int,[c.c_size_t]),
            ("bind",c.c_int,[c.c_size_t,c.c_void_p,c.c_int]),("getsockname",c.c_int,[c.c_size_t,c.c_void_p,c.c_void_p]),
            ("sendto",c.c_int,[c.c_size_t,c.c_void_p,c.c_int,c.c_int,c.c_void_p,c.c_int]),
            ("recvfrom",c.c_int,[c.c_size_t,c.c_void_p,c.c_int,c.c_int,c.c_void_p,c.c_void_p]),
            ("ioctlsocket",c.c_int,[c.c_size_t,c.c_int32,c.c_void_p]),
            ("WSACreateEvent",c.c_void_p,[]),("WSACloseEvent",c.c_int,[c.c_void_p]),
            ("WSASetEvent",c.c_int,[c.c_void_p]),("WSAResetEvent",c.c_int,[c.c_void_p]),
            ("WSAEventSelect",c.c_int,[c.c_size_t,c.c_void_p,c.c_int32]),
            ("WSAEnumNetworkEvents",c.c_int,[c.c_size_t,c.c_void_p,c.POINTER(NetworkEvents)]),
            ("WSAWaitForMultipleEvents",c.c_uint32,[c.c_uint32,c.c_void_p,c.c_int,c.c_uint32,c.c_int])]:
            fn=getattr(self.w,name); fn.restype=restype; fn.argtypes=args
        assert self.w.WSAStartup(0x202,c.create_string_buffer(512)) == 0
    def close(self):
        records=[]
        for s in self.sockets: records.append({"socket_close":self.w.closesocket(s)})
        for e in self.events: records.append({"event_close":self.w.WSACloseEvent(e)})
        records.append({"cleanup":self.w.WSACleanup()}); return records
    def address(self,port):
        return c.create_string_buffer(struct.pack("=H",2)+struct.pack("!H",port)+socket.inet_aton("127.0.0.1")+b"\0"*8)
    def udp(self):
        s=self.w.socket(2,2,17); assert s != c.c_size_t(-1).value
        self.sockets.append(s); assert self.w.bind(s,self.address(0),16) == 0
        addr=c.create_string_buffer(16); n=c.c_int(16); assert self.w.getsockname(s,addr,c.byref(n)) == 0
        return s,struct.unpack("!H",addr.raw[2:4])[0]
    def event(self):
        e=self.w.WSACreateEvent(); assert e; self.events.append(e); return e
    def register(self,s,e,mask):
        self.w.WSASetLastError(0x1234); result=self.w.WSAEventSelect(s,e,mask)
        return {"result":result,"error":self.w.WSAGetLastError(),"mask":mask,"event_null":not e}
    def wait(self,e,timeout=500):
        events=(c.c_void_p*1)(e); result=self.w.WSAWaitForMultipleEvents(1,events,0,timeout,0)
        return {"result":result,"error":self.w.WSAGetLastError()}
    def enum(self,s,e):
        result=NetworkEvents(); result.bits=0x35353535
        for i in range(10): result.errors[i]=SENTINEL
        self.w.WSASetLastError(0x1234)
        status=self.w.WSAEnumNetworkEvents(s,e,c.byref(result))
        return {"result":status,"error":self.w.WSAGetLastError(),"bits":result.bits,
                "errors":list(result.errors),"changed_error_indices":[i for i,v in enumerate(result.errors) if v!=SENTINEL]}
    def send(self,s,port,data):
        result=self.w.sendto(s,c.create_string_buffer(data),len(data),0,self.address(port),16)
        assert result==len(data),(result,self.w.WSAGetLastError()); return result
    def recv(self,s,flags=0):
        buf=c.create_string_buffer(128); self.w.WSASetLastError(0x1234)
        result=self.w.recvfrom(s,buf,128,flags,None,None)
        return {"result":result,"error":self.w.WSAGetLastError(),"data_hex":buf.raw[:max(result,0)].hex()}
    def blocking(self,s,nonblocking):
        value=c.c_uint32(nonblocking); self.w.WSASetLastError(0x1234)
        result=self.w.ioctlsocket(s,c.c_int32(0x8004667E),c.byref(value))
        return {"result":result,"error":self.w.WSAGetLastError(),"nonblocking":nonblocking}

def run_udp():
    o=Oracle(); out={"windows_version":platform.version(),"network_events_size":c.sizeof(NetworkEvents)}
    try:
        receiver,port=o.udp(); sender,_=o.udp(); e=o.event()
        out['udp_register_read_write']=o.register(receiver,e,3)
        out['initial_wait']=o.wait(e); out['initial_enum']=o.enum(receiver,e)
        out['after_initial_enum_wait']=o.wait(e,0)
        out['second_empty_enum']=o.enum(receiver,e)
        o.send(sender,port,b'first')
        out['data_arrival_wait']=o.wait(e); out['read_enum_before_recv']=o.enum(receiver,e)
        out['read_stays_disabled_without_recv']=o.wait(e,20)
        out['peek']=o.recv(receiver,2); out['peek_rearm_wait']=o.wait(e)
        out['peek_rearm_enum']=o.enum(receiver,e)
        out['consume']=o.recv(receiver); out['after_consume_wait']=o.wait(e,20)
        out['failed_recv']=o.recv(receiver)
        out['after_failed_recv_wait']=o.wait(e,20)
        o.send(sender,port,b'second')
        out['failed_recv_rearm_arrival_wait']=o.wait(e)
        out['failed_recv_rearm_enum']=o.enum(receiver,e); out['consume_second']=o.recv(receiver)
        out['blocking_while_selected']=o.blocking(receiver,0)
        o.send(sender,port,b'cancel-pending'); out['cancel_pending_wait']=o.wait(e)
        out['cancel_mask0']=o.register(receiver,e,0)
        out['cancel_clears_record_enum']=o.enum(receiver,e)
        out['cancel_event_state_after_enum']=o.wait(e,0)
        out['blocking_after_cancel']=o.blocking(receiver,0)
        out['restore_nonblocking']=o.blocking(receiver,1); out['consume_cancel_pending']=o.recv(receiver)
        # Null event keeps a network selection record but has no event to signal.
        out['select_null_event_nonzero_mask']=o.register(receiver,0,1)
        o.send(sender,port,b'null-event')
        import time; time.sleep(0.02)
        out['enum_null_event']=o.enum(receiver,0); out['consume_null_event']=o.recv(receiver)
        out['cancel_null_event']=o.register(receiver,0,0)
        # Shared event deliberately has pending records on two independent sockets.
        a,pa=o.udp(); b,pb=o.udp(); shared=o.event()
        out['shared_register_a']=o.register(a,shared,1); out['shared_register_b']=o.register(b,shared,1)
        o.send(sender,pa,b'A'); o.send(sender,pb,b'B')
        out['shared_wait_both_pending']=o.wait(shared); time.sleep(0.02)
        out['shared_enum_a']=o.enum(a,shared)
        out['shared_wait_after_enum_a_before_enum_b']=o.wait(shared,0)
        out['shared_enum_b']=o.enum(b,shared)
        out['shared_wait_after_enum_b']=o.wait(shared,0)
        out['shared_recv_a']=o.recv(a); out['shared_recv_b']=o.recv(b)
        # Enum with null event returns records without changing unrelated event state.
        o.send(sender,pa,b'nextA'); out['shared_new_data_wait']=o.wait(shared)
        out['enum_a_null_event']=o.enum(a,0)
        out['shared_wait_after_enum_null_event']=o.wait(shared,0)
        o.w.WSAResetEvent(shared); out['consume_nextA']=o.recv(a)
        invalid=c.c_size_t(-1).value
        out['invalid_socket_select']=o.register(invalid,e,1)
        out['invalid_socket_enum']=o.enum(invalid,e)
        out['invalid_event_select']=o.register(receiver,0x123456,1)
        out['invalid_event_enum']=o.enum(receiver,0x123456)
        # Failed replacement must not be trusted to preserve the old registration.
        out['cancel_after_invalid_event']=o.register(receiver,0,0)
    finally:
        out['owned_resources_cleanup']=o.close()
    return out

def main():
    out={"udp_and_shared_event":run_udp()}
    try:
        from wsa_event_select_tcp_windows import run_tcp
        out['tcp']=run_tcp()
    except ImportError:
        out['tcp']='Separate native TCP oracle not yet available.'
    out['scope']='Owned Windows loopback sockets/events only; no external connections or system settings changed.'
    out['sources']=[
      'https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaeventselect',
      'https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsaenumnetworkevents']
    path=Path(__file__).resolve().parents[1]/'research/wsa-event-select-windows.json'
    path.write_text(json.dumps(out,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(out,indent=2))

if __name__=='__main__': main()
