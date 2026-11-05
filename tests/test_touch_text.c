/* Real JNI text boundary and pending queue; synthetic UI token only in this fixture. */
#define UT3_ANDROID_APP
#include "../android/host/input.c"
#include <assert.h>
#include <stdio.h>

typedef struct { jsize length; const jchar *characters; } test_string;
static uint32_t ticks,characters[2048],back_events[2048];
static int count,back_count,logs,regions,exception,change_after_first;
uint32_t void_tick_ms(void) { return ++ticks; }
uint64_t void_monotonic_ns(void) { return (uint64_t)++ticks*1000000; }
uint32_t void_window_main(void) { return 1; }
void void_log(const char *format,...) { (void)format;++logs; }
void void_post_message(uint32_t window,uint32_t message,uint32_t value,uint32_t parameter)
{
    assert(window==1);
    if(message==0x102) {
        assert(parameter==1&&count<2048);characters[count++]=value;
        if(change_after_first) { change_after_first=0;__atomic_add_fetch(&touch_cursor_context,4,__ATOMIC_RELEASE); }
    }
    if((message==0x100||message==0x101)&&value==8) {
        assert(back_count<2048);
        assert(parameter==(message==0x100?0x000E0001u:0xC00E0001u));
        back_events[back_count++]=message;
    }
}
static jsize length(JNIEnv *env,jstring value) { (void)env;return ((test_string *)value)->length; }
static void region(JNIEnv *env,jstring value,jsize start,jsize amount,jchar *output)
{
    (void)env;++regions;assert(start==0&&amount>=0&&amount<=256);
    if(amount)memcpy(output,((test_string *)value)->characters,(size_t)amount*2);
}
static jboolean has_exception(JNIEnv *env) { (void)env;return exception; }
int main(void)
{
    const struct JNINativeInterface functions={.GetStringLength=length,.GetStringRegion=region,.ExceptionCheck=has_exception};
    JNIEnv env=&functions;
    jchar unicode[]={'A',0x416,0xD83D,0xDE00};test_string string={4,unicode};
    assert(!Java_org_ut3_port_GameActivity_nativeText(&env,NULL,(jstring)&string,0));
    touch_game_active=1;touch_cursor_context=7;
    assert(Java_org_ut3_port_GameActivity_nativeText(&env,NULL,(jstring)&string,7));
    void_input_key('E',1);void_input_touch(0,0,0,0,1);void_input_touch_reset();void_input_touch_reset();
    void_input_pump();assert(count==4&&!memcmp(characters,(uint32_t[]){'A',0x416,0xD83D,0xDE00},sizeof(uint32_t)*4));
    assert(void_input_key_state('E')<0); /* Text and pointer cancel preserve physical ownership. */
    assert(!Java_org_ut3_port_GameActivity_nativeText(&env,NULL,NULL,7));
    int copied=regions;string.length=257;assert(!Java_org_ut3_port_GameActivity_nativeText(&env,NULL,(jstring)&string,7)&&regions==copied);
    string.length=4;exception=1;int queued=pending_count;
    assert(!Java_org_ut3_port_GameActivity_nativeText(&env,NULL,(jstring)&string,7)&&pending_count==queued);exception=0;
    for(unsigned control=0;control<160;++control)if(control<32||control>=127) {
        uint16_t value=(uint16_t)control;assert(!text_submit(&value,1,7));
    }
    uint16_t malformed[][2]={{0xD800,0},{0xDC00,0},{0xD800,'x'},{0x2028,0},{0x2029,0}};
    for(unsigned i=0;i<sizeof malformed/sizeof *malformed;++i)assert(!text_submit(malformed[i],i==2?2:1,7));
    assert(text_submit(NULL,0,7)&&!text_submit(NULL,1,7));
    uint16_t full[256];for(unsigned i=0;i<256;++i)full[i]='x';
    assert(text_submit(full,256,7)&&pending_count==256);
    assert(text_submit(full,256,7)&&pending_count==512);
    uint32_t epoch=touch_epoch;assert(!text_submit(unicode,4,7)&&pending_count==512&&touch_epoch==epoch);
    int before=count;void_input_pump();assert(count==before+512&&!back_count);
    for(int i=0;i<512;++i)assert(characters[before+i]=='x');
    assert(text_submit(unicode,4,7));__atomic_store_n(&touch_cursor_context,11,__ATOMIC_RELEASE);
    before=count;void_input_pump();assert(count==before); /* Old scene is discarded. */
    assert(!text_submit(unicode,4,7));
    assert(text_submit(unicode,4,11));__atomic_store_n(&touch_cursor_context,14,__ATOMIC_RELEASE);
    void_input_pump();assert(count==before&&!text_submit(unicode,4,14));
    touch_cursor_context=19;touch_game_active=0;assert(!text_submit(unicode,4,19));
    touch_game_active=1;assert(text_submit(unicode,4,19));change_after_first=1;void_input_pump();assert(count==before+1&&characters[before]=='A');
    /* Text sends characters only: no Backspace of its own, and a physical one stays held. */
    void_input_key(8,1);void_input_pump();assert(back_count==1);
    assert(text_submit(unicode,4,touch_cursor_context));void_input_pump();
    assert(back_count==1&&void_input_key_state(8)<0);
    void_input_key(8,0);void_input_pump();assert(back_count==2&&back_events[1]==0x101u);
    assert(void_input_key_state('E')<0&&!logs);
    touch_game_active=1;touch_cursor_context=7;dialog_active=0;
    assert(!Java_org_ut3_port_GameActivity_nativeTextDialogRequest(&env,NULL,1,NULL,11,0));
    assert(Java_org_ut3_port_GameActivity_nativeTextDialogRequest(&env,NULL,1,NULL,7,0));
    assert(dialog_active&&dialog_command==1&&!dialog_ready);
    assert(!Java_org_ut3_port_GameActivity_nativeTextDialogRequest(&env,NULL,3,NULL,11,0)&&dialog_command==1);
    dialog_ready=1;dialog_flags=(32<<8)|1;dialog_target=0x123000;string.length=4;
    assert(!Java_org_ut3_port_GameActivity_nativeTextDialogRequest(&env,NULL,2,(jstring)&string,7,0x123004));
    assert(Java_org_ut3_port_GameActivity_nativeTextDialogRequest(&env,NULL,2,(jstring)&string,7,0x123000));
    assert(dialog_command==2&&!dialog_ready&&dialog_length==4&&!memcmp(dialog_request,unicode,8));
    assert(!Java_org_ut3_port_GameActivity_nativeTextDialogRequest(&env,NULL,2,(jstring)&string,7,0x123000));
    dialog_ready=1;dialog_flags=(3<<8)|1;
    assert(!Java_org_ut3_port_GameActivity_nativeTextDialogRequest(&env,NULL,2,(jstring)&string,7,0x123000));
    /* The console's line (4) commits without a target; text typed into a receiver (2) never commits. */
    dialog_ready=1;dialog_flags=(256<<8)|4;dialog_target=0;
    assert(!Java_org_ut3_port_GameActivity_nativeTextDialogRequest(&env,NULL,2,(jstring)&string,7,0x123000));
    assert(Java_org_ut3_port_GameActivity_nativeTextDialogRequest(&env,NULL,2,(jstring)&string,7,0));
    assert(dialog_command==2&&!dialog_ready&&dialog_length==4);
    dialog_ready=1;dialog_flags=(256<<8)|2|8;dialog_target=0;
    assert(!Java_org_ut3_port_GameActivity_nativeTextDialogRequest(&env,NULL,2,(jstring)&string,7,0));
    dialog_ready=1;dialog_flags=(256<<8);dialog_target=0;
    assert(!Java_org_ut3_port_GameActivity_nativeTextDialogRequest(&env,NULL,2,(jstring)&string,7,0));
    assert(Java_org_ut3_port_GameActivity_nativeTextDialogRequest(&env,NULL,3,NULL,7,0)&&dialog_command==3);
    assert(!Java_org_ut3_port_GameActivity_nativeTextDialogRequest(&env,NULL,99,NULL,7,0));
    puts("PASS JNI dialog: stale scene/target, request ordering, copied Unicode, replay/max-length rejection, console line without target, no commit of typed receivers, cancel isolation");
    puts("PASS JNI text: UTF16/Unicode pairs, control/surrogate/size/exception rejection, queue capacity, no Backspace from text, same-scene cancel survival, stale-game/UI rejection, physical ownership and zero text logs");
    return 0;
}
