/* Copyright (c) 2013-2014, The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of The Linux Foundation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#define LOG_TAG "listen_hal_loader"
/* #define LOG_NDEBUG 0 */
/* #define LOG_NDDEBUG 0 */
#include <stdbool.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <log/log.h>
#ifdef AUDIO_LISTEN_ENABLED
#include <listen_types.h>
#endif
#include "audio_hw.h"
#include "audio_extn.h"
#include "platform.h"
#include "platform_api.h"


#ifdef AUDIO_LISTEN_ENABLED

#define LIB_LISTEN_LOADER "/vendor/lib/liblistenhardware.so"

#define LISTEN_LOAD_SYMBOLS(dev, func_p, func_type, symbol) \
{\
    dev->func_p = (func_type)dlsym(dev->lib_handle,#symbol);\
    if (dev->func_p == NULL) {\
            ALOGE("%s: dlsym error %s for %s",\
                    __func__, dlerror(), #symbol);\
            free(dev);\
            dev = NULL;\
            return -EINVAL;\
    }\
}


typedef int (*create_listen_hw_t)(unsigned int snd_card,
                                  struct audio_route *audio_route);
typedef void (*destroy_listen_hw_t)();

typedef int (*open_listen_session_t)(struct audio_hw_device *,
                                    struct listen_open_params*,
                                    struct listen_session**);

typedef int (*close_listen_session_t)(struct audio_hw_device *dev,
                                    struct listen_session* handle);

typedef int (*set_mad_observer_t)(struct audio_hw_device *dev,
                                      listen_callback_t cb_func);

typedef int (*listen_set_parameters_t)(struct audio_hw_device *dev,
                                const char *kv_pairs);
typedef char* (*get_parameters_t)(const struct audio_hw_device *dev,
                                    const char *keys);
typedef void (*listen_notify_event_t)(event_type_t event_type);

struct listen_audio_device {
    void *lib_handle;
    struct audio_device *adev;

    create_listen_hw_t create_listen_hw;
    destroy_listen_hw_t destroy_listen_hw;
    open_listen_session_t open_listen_session;
    close_listen_session_t close_listen_session;
    set_mad_observer_t set_mad_observer;
    listen_set_parameters_t listen_set_parameters;
    get_parameters_t get_parameters;
    listen_notify_event_t notify_event;
};

static struct listen_audio_device *listen_dev;

void audio_extn_listen_update_status(snd_device_t snd_device,
                                    listen_event_type_t event)
{
    if (!platform_listen_update_status(snd_device)) {
        ALOGV("%s(): no need to notify listen. device = %s. Event = %u",
                __func__, platform_get_snd_device_name(snd_device), event);
        return;
    }

    if (listen_dev) {
        ALOGI("%s(): %s listen. current active device = %s. Event = %u",
                __func__,
                (event == LISTEN_EVENT_SND_DEVICE_BUSY) ? "stop" : "start",
                platform_get_snd_device_name(snd_device), event);

            if (event == LISTEN_EVENT_SND_DEVICE_FREE)
                   listen_dev->notify_event(AUDIO_CAPTURE_INACTIVE);
            else if (event == LISTEN_EVENT_SND_DEVICE_BUSY)
                   listen_dev->notify_event(AUDIO_CAPTURE_ACTIVE);
    }
}

void audio_extn_listen_set_parameters(struct audio_device *adev,
                               struct str_parms *parms)
{
    ALOGV("%s: enter", __func__);
    if (listen_dev) {
         char *kv_pairs = str_parms_to_str(parms);
         ALOGV_IF(kv_pairs != NULL, "%s: %s", __func__, kv_pairs);
         listen_dev->listen_set_parameters(&adev->device, kv_pairs);
         free(kv_pairs);
    }

    return;
}

int audio_extn_listen_init(struct audio_device *adev, unsigned int snd_card)
{
    int ret;
    void *lib_handle;

    ALOGI("%s: Enter", __func__);

    lib_handle = dlopen(LIB_LISTEN_LOADER, RTLD_NOW);

    if (lib_handle == NULL) {
        ALOGE("%s: DLOPEN failed for %s. error = %s", __func__, LIB_LISTEN_LOADER,
                dlerror());
        return -EINVAL;
    } else {
        ALOGI("%s: DLOPEN successful for %s", __func__, LIB_LISTEN_LOADER);

        listen_dev = (struct listen_audio_device*)
            calloc(1, sizeof(struct listen_audio_device));

        listen_dev->lib_handle = lib_handle;
        listen_dev->adev = adev;

        LISTEN_LOAD_SYMBOLS(listen_dev, create_listen_hw,
                create_listen_hw_t, create_listen_hw);

        LISTEN_LOAD_SYMBOLS(listen_dev, destroy_listen_hw,
                destroy_listen_hw_t, destroy_listen_hw);

        LISTEN_LOAD_SYMBOLS(listen_dev, open_listen_session,
                open_listen_session_t, open_listen_session);

        adev->device.open_listen_session = listen_dev->open_listen_session;

        LISTEN_LOAD_SYMBOLS(listen_dev, close_listen_session,
                close_listen_session_t, close_listen_session);

        adev->device.close_listen_session = listen_dev->close_listen_session;

        LISTEN_LOAD_SYMBOLS(listen_dev, set_mad_observer,
                set_mad_observer_t, set_mad_observer);

        adev->device.set_mad_observer = listen_dev->set_mad_observer;

        LISTEN_LOAD_SYMBOLS(listen_dev, listen_set_parameters,
                listen_set_parameters_t, listen_hw_set_parameters);

        adev->device.listen_set_parameters = listen_dev->listen_set_parameters;

        LISTEN_LOAD_SYMBOLS(listen_dev, get_parameters,
                get_parameters_t, listen_hw_get_parameters);

        LISTEN_LOAD_SYMBOLS(listen_dev, notify_event,
                listen_notify_event_t, listen_hw_notify_event);

        listen_dev->create_listen_hw(snd_card, adev->audio_route);
    }
    return 0;
}

void audio_extn_listen_deinit(struct audio_device *adev)
{
    ALOGI("%s: Enter", __func__);

    if (listen_dev && (listen_dev->adev == adev) && listen_dev->lib_handle) {
        listen_dev->destroy_listen_hw();
        dlclose(listen_dev->lib_handle);
        free(listen_dev);
        listen_dev = NULL;
    }
}

#endif /* AUDIO_LISTEN_ENABLED */
