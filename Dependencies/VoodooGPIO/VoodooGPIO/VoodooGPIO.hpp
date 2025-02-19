//
//  VoodooGPIO.h
//  VoodooGPIO
//
//  Created by CoolStar on 8/14/17.
//  Copyright © 2017 CoolStar. All rights reserved.
//

#include <IOKit/IOLib.h>
#include <IOKit/IOKitKeys.h>
#include <IOKit/acpi/IOACPIPlatformDevice.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOInterruptEventSource.h>
#include <IOKit/IOLocks.h>
#include <IOKit/IOCommandGate.h>
#include "linuxirq.h"

#ifndef VoodooGPIO_h
#define VoodooGPIO_h

// Exists in the macOS 10.15 SDK
#ifndef IOSafeDeleteNULL
#define IOSafeDeleteNULL(ptr, type, count)              \
    do {                                                \
        if (NULL != (ptr)) {                            \
            IODelete((ptr), type, count);               \
            (ptr) = NULL;                               \
        }                                               \
    } while (0)
#endif

struct pinctrl_pin_desc {
    unsigned number;
    char *name;
    void *drv_data;
};

#define PINCTRL_PIN(a, b) {.number = a, .name = b}
#define PINCTRL_PIN_ANON(a) {.number = a}

/**
 * struct intel_pingroup - Description about group of pins
 * @name: Name of the groups
 * @pins: All pins in this group
 * @npins: Number of pins in this groups
 * @mode: Native mode in which the group is muxed out @pins. Used if @modes
 *        is %NULL.
 * @modes: If not %NULL this will hold mode for each pin in @pins
 */
struct intel_pingroup {
    char *name;
    unsigned *pins;
    size_t npins;
    unsigned short mode;
    unsigned *modes;
};

/**
 * struct intel_function - Description about a function
 * @name: Name of the function
 * @groups: An array of groups for this function
 * @ngroups: Number of groups in @groups
 */
struct intel_function {
    char *name;
    char * const *groups;
    size_t ngroups;
};

/**
 * struct intel_padgroup - Hardware pad group information
 * @reg_num: GPI_IS register number
 * @base: Starting pin of this group
 * @size: Size of this group (maximum is 32).
 * @gpio_base: Starting GPIO base of this group (%0 if matches with @base,
 *             and %-1 if no GPIO mapping should be created)
 * @padown_num: PAD_OWN register number (assigned by the core driver)
 *
 * If pad groups of a community are not the same size, use this structure
 * to specify them.
 */
struct intel_padgroup {
    UInt32 reg_num;
    UInt32 base;
    UInt32 size;
    SInt32 gpio_base;
    UInt32 padown_num;
};

/**
 * struct intel_community - Intel pin community description
 * @barno: MMIO BAR number where registers for this community reside
 * @padown_offset: Register offset of PAD_OWN register from @regs. If %0
 *                 then there is no support for owner.
 * @padcfglock_offset: Register offset of PADCFGLOCK from @regs. If %0 then
 *                     locking is not supported.
 * @hostown_offset: Register offset of HOSTSW_OWN from @regs. If %0 then it
 *                  is assumed that the host owns the pin (rather than
 *                  ACPI).
 * @ie_offset: Register offset of GPI_IE from @regs.
 * @pin_base: Starting pin of pins in this community
 * @gpp_size: Maximum number of pads in each group, such as PADCFGLOCK,
 *            HOSTSW_OWN,  GPI_IS, GPI_IE, etc. Used when @gpps is %NULL.
 * @gpp_num_padown_regs: Number of pad registers each pad group consumes at
 *			 minimum. Use %0 if the number of registers can be
 *			 determined by the size of the group.
 * @npins: Number of pins in this community
 * @features: Additional features supported by the hardware
 * @gpps: Pad groups if the controller has variable size pad groups
 * @ngpps: Number of pad groups in this community
 * @regs: Community specific common registers (reserved for core driver)
 * @pad_regs: Community specific pad registers (reserved for core driver)
 *
 * Most Intel GPIO host controllers this driver supports each pad group is
 * of equal size (except the last one). In that case the driver can just
 * fill in @gpp_size field and let the core driver to handle the rest. If
 * the controller has pad groups of variable size the client driver can
 * pass custom @gpps and @ngpps instead.
 */
struct intel_community {
    unsigned barno;
    unsigned padown_offset;
    unsigned padcfglock_offset;
    unsigned hostown_offset;
    unsigned ie_offset;
    unsigned pin_base;
    unsigned gpp_size;
    unsigned gpp_num_padown_regs;
    size_t npins;
    unsigned features;
    struct intel_padgroup *gpps;
    size_t ngpps;
    bool gpps_alloc;
    bool *isActiveCommunity;
    /* Reserved for the core driver */
    IOMemoryMap *mmap;
    IOVirtualAddress regs;
    IOVirtualAddress pad_regs;

    unsigned *interruptTypes;
    OSObject **pinInterruptActionOwners;
    IOInterruptAction *pinInterruptAction;
    void **pinInterruptRefcons;
};

struct intel_pad_context {
    uint32_t padcfg0;
    uint32_t padcfg1;
    uint32_t padcfg2;
};

struct intel_community_context {
    uint32_t *intmask;
    uint32_t *hostown;
};

struct intel_pinctrl_context {
    struct intel_pad_context *pads;
    struct intel_community_context *communities;
};

/* Additional features supported by the hardware */
#define PINCTRL_FEATURE_DEBOUNCE    1
#define PINCTRL_FEATURE_1K_PD       2

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

/**
 * PIN_GROUP - Declare a pin group
 * @n: Name of the group
 * @p: An array of pins this group consists
 * @m: Mode which the pins are put when this group is active. Can be either
 *     a single integer or an array of integers in which case mode is per
 *     pin.
 */
#define PIN_GROUP(n, p, m)                          \
{                                                   \
    .name = (n),                                    \
    .pins = (p),                                    \
    .npins = ARRAY_SIZE((p)),                       \
    .mode = __builtin_choose_expr(                  \
            __builtin_constant_p((m)), (m), 0),     \
    .modes = __builtin_choose_expr(                 \
            __builtin_constant_p((m)), NULL, (m)),  \
}

#define FUNCTION(n, g)          \
{                               \
    .name = (n),                \
    .groups = (g),              \
    .ngroups = ARRAY_SIZE((g)), \
}

class VoodooGPIO : public IOService {
    OSDeclareDefaultStructors(VoodooGPIO);

 protected:
    struct pinctrl_pin_desc *pins;
    size_t npins;
    struct intel_pingroup *groups;
    size_t ngroups;
    struct intel_function *functions;
    size_t nfunctions;
    struct intel_community *communities;
    size_t ncommunities;

 private:
    struct intel_pinctrl_context context;

    bool controllerIsAwake;

    IOWorkLoop *workLoop = nullptr;
    IOCommandGate* command_gate = nullptr;
    OSArray* registered_pin_list = nullptr;
    bool isInterruptBusy;
    UInt32 nInactiveCommunities;

    UInt32 readl(IOVirtualAddress addr);
    void writel(UInt32 b, IOVirtualAddress addr);

    IOWorkLoop* getWorkLoop();

    struct intel_community *intel_get_community(unsigned pin);
    struct intel_padgroup *intel_community_get_padgroup(struct intel_community *community, unsigned pin);
    IOVirtualAddress intel_get_padcfg(unsigned pin, unsigned reg);

    bool intel_pad_owned_by_host(unsigned pin);
    bool intel_pad_acpi_mode(unsigned pin);
    int intel_pad_locked(unsigned pin);
    bool intel_pad_is_unlocked(unsigned int pin);

    SInt32 intel_gpio_to_pin(UInt32 offset,
                             struct intel_community **community,
                             struct intel_padgroup **padgrp);
    void intel_gpio_irq_mask_unmask(unsigned pin, bool mask);
    bool intel_gpio_irq_set_type(unsigned pin, unsigned type);

    bool intel_pinctrl_add_padgroups(intel_community *community);

    bool intel_pinctrl_should_save(unsigned pin);
    void intel_pinctrl_pm_init();
    void intel_pinctrl_pm_release();
    void intel_pinctrl_suspend();
    void intel_gpio_irq_init();
    UInt32 intel_gpio_is_requested(int base, unsigned int size);
    UInt32 intel_gpio_update_pad_mode(IOVirtualAddress hostown, UInt32 mask, UInt32 value);
    void intel_pinctrl_resume();

    void intel_gpio_community_irq_handler(struct intel_community *community, bool *firstdelay);
    void intel_gpio_pin_irq_handler(unsigned hw_pin);

    void InterruptOccurred(void *refCon, IOService *nub, int source);
    IOReturn interruptOccurredGated();

 public:
    IOReturn getInterruptType(int pin, int *interruptType) override;
    IOReturn registerInterrupt(int pin, OSObject *target, IOInterruptAction handler, void *refcon) override;
    IOReturn unregisterInterrupt(int pin) override;

    IOReturn enableInterrupt(int pin) override;
    IOReturn disableInterrupt(int pin) override;

    IOReturn setInterruptTypeForPin(int pin, int type);

    bool init(OSDictionary* properties) override;

    bool start(IOService *provider) override;
    void stop(IOService *provider) override;

    IOReturn setPowerState(unsigned long powerState, IOService *whatDevice) override;
    
    bool getPinStatus(int pin);
};

#endif /* VoodooGPIO_h */
