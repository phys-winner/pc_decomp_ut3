/* Equivalent of mods/fix_mp/dinput8.dll, adapted to the pinned Steam image. */
#include "host.h"

typedef struct { uint32_t address; const char *before, *after; } openspy_patch;
static const openspy_patch patches[]={
    {0x01D2AE70,"u3.epicgames.com","gpsp.openspy.net"},
    {0x01D2A738,"u3.epicgames.com","gpcm.openspy.net"},
    {0x01D27DB8,"u3.epicgames.com","%s.available.openspy.net"},
    {0x01D2E18C,"u3.epicgames.com","%s.ms%d.openspy.net"},
    {0x01B3D338,"http://%s.sake.gamespy.com/SakeStorageServer/StorageServer.asmx","http://%s.sake.openspy.net/SakeStorageServer/StorageServer.asmx"},
    {0x01D27CA0,"http://%s.sake.gamespy.com/SakeStorageServer/StorageServer.asmx","http://%s.sake.openspy.net/SakeStorageServer/StorageServer.asmx"},
    {0x01B3D3E8,"http://%s.comp.pubsvs.gamespy.com/CompetitionService/CompetitionService.asmx","http://%s.comp.pubsvs.openspy.net/CompetitionService/CompetitionService.asmx"},
    {0x01D27AD0,"http://%s.comp.pubsvs.gamespy.com/CompetitionService/CompetitionService.asmx","http://%s.comp.pubsvs.openspy.net/CompetitionService/CompetitionService.asmx"},
    {0x01D29634,"u3.epicgames.com","%s.master.openspy.net"},
    {0x01D27E90,"http://motd.gamespy.com/motd/motd.asp","http://motd.openspy.net/motd/motd.asp"},
    {0x01D27E28,"http://motd.gamespy.com/motd/vercheck.asp","http://motd.openspy.net/motd/vercheck.asp"},
    {0x01D27F08,"http://motd.gamespy.com/motd/vercheck.asp","http://motd.openspy.net/motd/vercheck.asp"},
    {0x01D27380,"https://%s.auth.pubsvs.gamespy.com/AuthService/AuthService.asmx","http://%s.auth.pubsvs.openspy.net/AuthService/AuthService.asmx"},
    {0x01B47550,
     "BF05D63E93751AD4A59A4A7389CF0BE8A22CCDEEA1E7F12C062D6E194472EFDA5184CCECEB4FBADF5EB1D7ABFE91181453972AA971F624AF9BA8F0F82E2869FB7D44BDE8D56EE50977898F3FEE75869622C4981F07506248BD3D092E8EA05C12B2FA37881176084C8F8B8756C4722CDC57D2AD28ACD3AD85934FB48D6B2D2027",
     "afb5818995b3708d0656a5bdd20760aee76537907625f6d23f40bf17029e56808d36966c0804e1d797e310fedd8c06e6c4121d963863d765811fc9baeb2315c9a6eaeb125fad694d9ea4d4a928f223d9f4514533f18a5432dd0435c5c6ac8e276cf29489cb5ac880f16b0d7832ee927d4e27d622d6a450cd1560d7fa882c6c13"}
};

int void_openspy_init(void)
{
    /* Validate every patch before changing any: PE loading already pins the whole original image. */
    for(unsigned i=0;i<sizeof patches/sizeof *patches;i++) {
        const openspy_patch *p=patches+i;
        size_t before=strlen(p->before)+1,after=strlen(p->after)+1;
        if(!void_vm_readable(p->address,before)||!void_vm_readable(p->address,after)||
           memcmp(GPTR(p->address),p->before,before)) {
            void_log("[openspy] original data mismatch at %08X",p->address);return 0;
        }
    }
    for(unsigned i=0;i<sizeof patches/sizeof *patches;i++) {
        uint32_t old,ignored;
        if(!void_vm_protect(patches[i].address,(uint32_t)strlen(patches[i].after)+1,4,&old))return 0;
        memcpy(GPTR(patches[i].address),patches[i].after,strlen(patches[i].after)+1);
        if(!void_vm_protect(patches[i].address,(uint32_t)strlen(patches[i].after)+1,old,&ignored))return 0;
    }
    void_log("[openspy] services and authentication key installed; lowercase MD5 call sites compiled natively");
    return 1;
}

void void_openspy_md5(void)
{
    static const char digits[]="0123456789abcdef";
    uint32_t source=ARG(0),out=ARG(1);
    if(!void_vm_readable(source,16)||!void_vm_writable(out,33))
        void_lift_trap(0x01243A50u,"OpenSpy MD5 buffer is invalid");
    uint8_t digest[16];memcpy(digest,GPTR(source),16);
    for(unsigned i=0;i<16;i++) {
        MEM8(out+2*i)=digits[digest[i]>>4];MEM8(out+2*i+1)=digits[digest[i]&15];
    }
    MEM8(out+32)=0;
    g_eax=16;g_esp+=4;
}
