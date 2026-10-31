/* UI thread records intent; only the VM-owned publisher reader delivers it. */
#include "host.h"
#ifdef UT3_ANDROID_APP
#include <jni.h>
#endif

static int port_menu_requested;

int void_touch_menu_pending(void)
{
    return __atomic_load_n(&port_menu_requested,__ATOMIC_ACQUIRE);
}

void void_touch_menu_delivered(void)
{
    __atomic_store_n(&port_menu_requested,0,__ATOMIC_RELEASE);
}

#ifdef UT3_ANDROID_APP
JNIEXPORT void JNICALL Java_org_ut3_port_GameActivity_nativeSettings(JNIEnv *env,jclass cls)
{
    (void)env;(void)cls;
    __atomic_store_n(&port_menu_requested,1,__ATOMIC_RELEASE);
}
#endif
