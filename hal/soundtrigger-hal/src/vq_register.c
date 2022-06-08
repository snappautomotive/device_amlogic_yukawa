#include <stdbool.h>

#include "vq_hal.h"
#include "vq_plugin.h"

extern struct vq_plugin algo_plugin;

struct vq_plugin_register vq_registrar[] =
{
    { "Algo", &algo_plugin},
};

unsigned int vq_registrar_size() {
    unsigned int count = (sizeof(vq_registrar) /
                            sizeof(struct vq_plugin_register));
    return count;
}
