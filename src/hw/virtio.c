#include "vibe.h"

void hw_virtio_init(hw_virtio *vio, mach *m, u32 device_id) {
    memset(vio, 0, sizeof(*vio));
    vio->mach = m;
    vio->device_id = device_id;
    vio->vendor_id = VIRTIO_VENDOR_ID;
    vio->device_features = (1ULL << VIRTIO_F_VERSION_1);
    
    for (int i = 0; i < 8; i++) {
        vio->queues[i].num_max = 256;
    }
}

static u32 hw_virtio_read_config(hw_virtio *vio, u32 offset) {
    switch (offset) {
    case VIRTIO_MMIO_MAGIC_VALUE:
        return VIRTIO_MAGIC;
    case VIRTIO_MMIO_VERSION:
        return VIRTIO_VERSION;
    case VIRTIO_MMIO_DEVICE_ID:
        return vio->device_id;
    case VIRTIO_MMIO_VENDOR_ID:
        return vio->vendor_id;
    case VIRTIO_MMIO_DEVICE_FEATURES:
        if (vio->device_features_sel == 0)
            return vio->device_features & 0xFFFFFFFF;
        else if (vio->device_features_sel == 1)
            return (vio->device_features >> 32) & 0xFFFFFFFF;
        return 0;
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
        return vio->device_features_sel;
    case VIRTIO_MMIO_DRIVER_FEATURES:
        return 0;
    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
        return vio->driver_features_sel;
    case VIRTIO_MMIO_QUEUE_SEL:
        return vio->queue_sel;
    case VIRTIO_MMIO_QUEUE_NUM_MAX:
        if (vio->queue_sel < 8)
            return vio->queues[vio->queue_sel].num_max;
        return 0;
    case VIRTIO_MMIO_QUEUE_NUM:
        if (vio->queue_sel < 8)
            return vio->queues[vio->queue_sel].num;
        return 0;
    case VIRTIO_MMIO_QUEUE_READY:
        if (vio->queue_sel < 8)
            return vio->queues[vio->queue_sel].ready ? 1 : 0;
        return 0;
    case VIRTIO_MMIO_INTERRUPT_STATUS:
        return vio->interrupt_status;
    case VIRTIO_MMIO_INTERRUPT_ACK:
        return 0;
    case VIRTIO_MMIO_STATUS:
        return vio->status;
    case VIRTIO_MMIO_QUEUE_DESC_LOW:
    case VIRTIO_MMIO_QUEUE_DESC_HIGH:
    case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
    case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
    case VIRTIO_MMIO_QUEUE_USED_LOW:
    case VIRTIO_MMIO_QUEUE_USED_HIGH:
        return 0;
    case VIRTIO_MMIO_CONFIG_GENERATION:
        return vio->config_generation;
    default:
        if (offset >= VIRTIO_MMIO_CONFIG && vio->get_config) {
            return vio->get_config(vio, offset - VIRTIO_MMIO_CONFIG);
        }
        return 0;
    }
}

static void hw_virtio_write_config(hw_virtio *vio, u32 offset, u32 value) {
    switch (offset) {
    case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
        vio->device_features_sel = value;
        break;
    case VIRTIO_MMIO_DRIVER_FEATURES:
        if (vio->driver_features_sel == 0)
            vio->driver_features = (vio->driver_features & 0xFFFFFFFF00000000ULL) | value;
        else if (vio->driver_features_sel == 1)
            vio->driver_features = (vio->driver_features & 0xFFFFFFFFULL) | ((u64)value << 32);
        break;
    case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
        vio->driver_features_sel = value;
        break;
    case VIRTIO_MMIO_QUEUE_SEL:
        vio->queue_sel = value;
        break;
    case VIRTIO_MMIO_QUEUE_NUM:
        if (vio->queue_sel < 8 && value <= vio->queues[vio->queue_sel].num_max)
            vio->queues[vio->queue_sel].num = value;
        break;
    case VIRTIO_MMIO_QUEUE_READY:
        if (vio->queue_sel < 8)
            vio->queues[vio->queue_sel].ready = value & 1;
        break;
    case VIRTIO_MMIO_QUEUE_NOTIFY:
        if (value < 8 && vio->queue_notify)
            vio->queue_notify(vio, value);
        break;
    case VIRTIO_MMIO_INTERRUPT_ACK:
        vio->interrupt_status &= ~value;
        break;
    case VIRTIO_MMIO_STATUS:
        if (value == 0 && vio->reset) {
            vio->reset(vio);
            vio->status = 0;
            vio->interrupt_status = 0;
            for (int i = 0; i < 8; i++) {
                vio->queues[i].ready = false;
            }
        } else {
            vio->status = value;
        }
        break;
    case VIRTIO_MMIO_QUEUE_DESC_LOW:
        if (vio->queue_sel < 8)
            vio->queues[vio->queue_sel].desc_addr = 
                (vio->queues[vio->queue_sel].desc_addr & 0xFFFFFFFF00000000ULL) | value;
        break;
    case VIRTIO_MMIO_QUEUE_DESC_HIGH:
        if (vio->queue_sel < 8)
            vio->queues[vio->queue_sel].desc_addr = 
                (vio->queues[vio->queue_sel].desc_addr & 0xFFFFFFFFULL) | ((u64)value << 32);
        break;
    case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
        if (vio->queue_sel < 8)
            vio->queues[vio->queue_sel].avail_addr = 
                (vio->queues[vio->queue_sel].avail_addr & 0xFFFFFFFF00000000ULL) | value;
        break;
    case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
        if (vio->queue_sel < 8)
            vio->queues[vio->queue_sel].avail_addr = 
                (vio->queues[vio->queue_sel].avail_addr & 0xFFFFFFFFULL) | ((u64)value << 32);
        break;
    case VIRTIO_MMIO_QUEUE_USED_LOW:
        if (vio->queue_sel < 8)
            vio->queues[vio->queue_sel].used_addr = 
                (vio->queues[vio->queue_sel].used_addr & 0xFFFFFFFF00000000ULL) | value;
        break;
    case VIRTIO_MMIO_QUEUE_USED_HIGH:
        if (vio->queue_sel < 8)
            vio->queues[vio->queue_sel].used_addr = 
                (vio->queues[vio->queue_sel].used_addr & 0xFFFFFFFFULL) | ((u64)value << 32);
        break;
    default:
        if (offset >= VIRTIO_MMIO_CONFIG && vio->set_config) {
            vio->set_config(vio, offset - VIRTIO_MMIO_CONFIG, value);
        }
        break;
    }
}

bus_error hw_virtio_mmio_bus(hw_virtio *vio, u32 addr, u8 *data, bool is_store, u32 width) {
    addr &= 0xFFF;
    
    if (addr < VIRTIO_MMIO_CONFIG) {
        if (width != 4)
            return BUS_ALIGN;
    } else {
        if (addr >= VIRTIO_MMIO_CONFIG && (width == 1 || width == 2 || width == 4)) {
        } else {
            return BUS_ALIGN;
        }
    }
    
    if (is_store) {
        u32 value = 0;
        if (width == 1) {
            value = data[0];
        } else if (width == 2) {
            rv_endcvt(data, (u8*)&value, 2, 0);
        } else {
            rv_endcvt(data, (u8*)&value, 4, 0);
        }
        hw_virtio_write_config(vio, addr, value);
    } else {
        if (addr >= VIRTIO_MMIO_CONFIG && vio->get_config) {
            // For config space, handle multi-byte access
            if (width == 1) {
                u32 value = vio->get_config(vio, addr - VIRTIO_MMIO_CONFIG);
                data[0] = value & 0xFF;
            } else if (width == 2) {
                u16 val16 = 0;
                val16 |= vio->get_config(vio, addr - VIRTIO_MMIO_CONFIG);
                val16 |= vio->get_config(vio, addr - VIRTIO_MMIO_CONFIG + 1) << 8;
                rv_endcvt((u8*)&val16, data, 2, 1);
            } else if (width == 4) {
                u32 val32 = 0;
                val32 |= vio->get_config(vio, addr - VIRTIO_MMIO_CONFIG);
                val32 |= vio->get_config(vio, addr - VIRTIO_MMIO_CONFIG + 1) << 8;
                val32 |= vio->get_config(vio, addr - VIRTIO_MMIO_CONFIG + 2) << 16;
                val32 |= vio->get_config(vio, addr - VIRTIO_MMIO_CONFIG + 3) << 24;
                rv_endcvt((u8*)&val32, data, 4, 1);
            }
        } else {
            u32 value = hw_virtio_read_config(vio, addr);
            if (width == 1) {
                data[0] = value & 0xFF;
            } else if (width == 2) {
                u16 val16 = value & 0xFFFF;
                rv_endcvt((u8*)&val16, data, 2, 1);
            } else {
                rv_endcvt((u8*)&value, data, 4, 1);
            }
        }
    }
    
    return BUS_OK;
}

void hw_virtio_raise_interrupt(hw_virtio *vio, u32 flags) {
    vio->interrupt_status |= flags;
    hw_plic_irq(&vio->mach->plic0, vio->irq_num);
}