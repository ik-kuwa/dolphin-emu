// Copyright 2013 Dolphin Emulator Project
// Licensed under GPLv2
// Refer to the license.txt file included.

#include "../Core.h"
#include "../Debugger/Debugger_SymbolMap.h"
#include "../HW/WII_IPC.h"
#include "WII_IPC_HLE.h"
#include "WII_IPC_HLE_Device_hid.h"
#include "errno.h"
#include "ConfigManager.h"

#define MAX_DEVICE_DEVNUM 256
static u64 hidDeviceAliases[MAX_DEVICE_DEVNUM];

// Regular thread
void CWII_IPC_HLE_Device_hid::checkUsbUpdates(CWII_IPC_HLE_Device_hid* hid)
{
	timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 500;
	while (hid->usb_thread_running)
	{

		static u16 timeToFill = 0;
		if (timeToFill == 0)
		{
			std::lock_guard<std::mutex> lk(hid->s_device_list_reply);
			if (hid->deviceCommandAddress != 0){
				hid->FillOutDevices(Memory::Read_U32(hid->deviceCommandAddress + 0x18), Memory::Read_U32(hid->deviceCommandAddress + 0x1C));

				Memory::Write_U32(8, hid->deviceCommandAddress);
				// IOS seems to write back the command that was responded to
				Memory::Write_U32(/*COMMAND_IOCTL*/ 6, hid->deviceCommandAddress + 8);

				// Return value
				Memory::Write_U32(0, hid->deviceCommandAddress + 4);

				WII_IPC_HLE_Interface::EnqueReplyCallback(hid->deviceCommandAddress);
				hid->deviceCommandAddress = 0;
			}
		}
		timeToFill+=8;
		libusb_handle_events_timeout(NULL, &tv);
	}

	return;
}

void CWII_IPC_HLE_Device_hid::handleUsbUpdates(struct libusb_transfer *transfer)
{
	int ret = HIDERR_NO_DEVICE_FOUND;
	u32 replyAddress = (u32)(size_t)transfer->user_data;
	if (transfer->status == LIBUSB_TRANSFER_COMPLETED)
	{
		ret = transfer->length;
	}

	Memory::Write_U32(8, replyAddress);
	// IOS seems to write back the command that was responded to
	Memory::Write_U32(/*COMMAND_IOCTL*/ 6, replyAddress + 8);

	// Return value
	Memory::Write_U32(ret, replyAddress + 4);

	WII_IPC_HLE_Interface::EnqueReplyCallback(replyAddress);
	//WARN_LOG(WII_IPC_HID, "OMG OMG OMG I GOT A CALLBACK, IMMA BE FAMOUS %d %d %d", transfer->actual_length, transfer->length, transfer->status);
}


CWII_IPC_HLE_Device_hid::CWII_IPC_HLE_Device_hid(u32 _DeviceID, const std::string& _rDeviceName)
	: IWII_IPC_HLE_Device(_DeviceID, _rDeviceName)
{
	deviceCommandAddress = 0;
	memset(hidDeviceAliases, 0, sizeof(hidDeviceAliases));
	int ret = libusb_init(NULL);
	if (ret)
	{
		ERROR_LOG(WII_IPC_HID, "libusb_init failed with error: %d", ret);
	}
	else
	{
		usb_thread_running = true;
		usb_thread = std::thread(checkUsbUpdates, this);
	}
}

CWII_IPC_HLE_Device_hid::~CWII_IPC_HLE_Device_hid()
{
	bool deinit_libusb = false;
	if (usb_thread_running)
	{
		usb_thread_running = false;
		usb_thread.join();
		deinit_libusb = true;
	}

	for ( std::map<u32,libusb_device_handle*>::const_iterator iter = open_devices.begin(); iter != open_devices.end(); ++iter )
	{
		libusb_close(iter->second);
	}
	open_devices.clear();

	if (deinit_libusb)
		libusb_exit(NULL);
}

bool CWII_IPC_HLE_Device_hid::Open(u32 _CommandAddress, u32 _Mode)
{
	WARN_LOG(WII_IPC_HID, "HID::Open");
	m_Active = true;
	Memory::Write_U32(GetDeviceID(), _CommandAddress + 4);
	return true;
}

bool CWII_IPC_HLE_Device_hid::Close(u32 _CommandAddress, bool _bForce)
{
	WARN_LOG(WII_IPC_HID, "HID::Close");
	m_Active = false;
	if (!_bForce)
		Memory::Write_U32(0, _CommandAddress + 4);
	return true;
}

u32 CWII_IPC_HLE_Device_hid::Update()
{

	u32 work_done = 0;
	return work_done;
}

bool CWII_IPC_HLE_Device_hid::IOCtl(u32 _CommandAddress)
{
	u32 Parameter		= Memory::Read_U32(_CommandAddress + 0xC);
	u32 BufferIn		= Memory::Read_U32(_CommandAddress + 0x10);
	u32 BufferInSize	= Memory::Read_U32(_CommandAddress + 0x14);
	u32 BufferOut		= Memory::Read_U32(_CommandAddress + 0x18);
	u32 BufferOutSize	= Memory::Read_U32(_CommandAddress + 0x1C);

	u32 ReturnValue = 0;
	switch (Parameter)
	{
	case IOCTL_HID_GET_ATTACHED:
	{
		WARN_LOG(WII_IPC_HID, "HID::IOCtl(Get Attached) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			BufferIn, BufferInSize, BufferOut, BufferOutSize);
		deviceCommandAddress = _CommandAddress;
		return false;
		break;
	}
	case IOCTL_HID_OPEN:
	{
		WARN_LOG(WII_IPC_HID, "HID::IOCtl(Open) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			BufferIn, BufferInSize, BufferOut, BufferOutSize);

		//hid version, apparently
		ReturnValue = 0x40001;
		break;
	}
	case IOCTL_HID_SET_SUSPEND:
	{
		WARN_LOG(WII_IPC_HID, "HID::IOCtl(Set Suspend) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			BufferIn, BufferInSize, BufferOut, BufferOutSize);
		// not actually implemented in IOS
		ReturnValue = 0;
		break;
	}
	case IOCTL_HID_CANCEL_INTERRUPT:
	{
		WARN_LOG(WII_IPC_HID, "HID::IOCtl(Cancel Interrupt) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			BufferIn, BufferInSize, BufferOut, BufferOutSize);
		ReturnValue = 0;
		break;
	}
	case IOCTL_HID_CONTROL:
	{
		/*
			ERROR CODES:
			-4 Cant find device specified
		*/

		u32 dev_num  = Memory::Read_U32(BufferIn+0x10);
		u8 bmRequestType = Memory::Read_U8(BufferIn+0x14);
		u8 bRequest = Memory::Read_U8(BufferIn+0x15);
		u16 wValue = Memory::Read_U16(BufferIn+0x16);
		u16 wIndex = Memory::Read_U16(BufferIn+0x18);
		u16 wLength = Memory::Read_U16(BufferIn+0x1A);
		u32 data = Memory::Read_U32(BufferIn+0x1C);

		ReturnValue = HIDERR_NO_DEVICE_FOUND;

		libusb_device_handle * dev_handle = GetDeviceByDevNum(dev_num);

		if (dev_handle == NULL)
		{
			WARN_LOG(WII_IPC_HID, "Could not find handle: %X", dev_num);
			break;
		}
		struct libusb_transfer *transfer = libusb_alloc_transfer(0);
		transfer->flags |= LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;

		u8 * buffer = (u8*)malloc(wLength + LIBUSB_CONTROL_SETUP_SIZE);
		libusb_fill_control_setup(buffer, bmRequestType, bRequest, wValue, wIndex, wLength);
		memcpy(buffer + LIBUSB_CONTROL_SETUP_SIZE, Memory::GetPointer(data), wLength);
		libusb_fill_control_transfer(transfer, dev_handle, buffer, handleUsbUpdates, (void*)(size_t)_CommandAddress, /* no timeout */ 0);
		libusb_submit_transfer(transfer);

		//WARN_LOG(WII_IPC_HID, "HID::IOCtl(Control)(%02X, %02X) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
		//		  bmRequestType, bRequest, BufferIn, BufferInSize, BufferOut, BufferOutSize);

		// It's the async way!
		return false;
		break;
	}
	case IOCTL_HID_INTERRUPT_OUT:
	case IOCTL_HID_INTERRUPT_IN:
	{
		u32 dev_num  = Memory::Read_U32(BufferIn+0x10);
		u32 endpoint = Memory::Read_U32(BufferIn+0x14);
		u32 length = Memory::Read_U32(BufferIn+0x18);

		u32 data = Memory::Read_U32(BufferIn+0x1C);

		ReturnValue = HIDERR_NO_DEVICE_FOUND;

		libusb_device_handle * dev_handle = GetDeviceByDevNum(dev_num);

		if (dev_handle == NULL)
		{
			WARN_LOG(WII_IPC_HID, "Could not find handle: %X", dev_num);
			break;
		}

		struct libusb_transfer *transfer = libusb_alloc_transfer(0);
		transfer->flags |= LIBUSB_TRANSFER_FREE_TRANSFER;
		libusb_fill_interrupt_transfer(transfer, dev_handle, endpoint, Memory::GetPointer(data), length,
									   handleUsbUpdates, (void*)(size_t)_CommandAddress, 0);
		libusb_submit_transfer(transfer);

		//WARN_LOG(WII_IPC_HID, "HID::IOCtl(Interrupt %s)(%d,%d,%X) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
		//	Parameter == IOCTL_HID_INTERRUPT_IN ? "In" : "Out", endpoint, length, data, BufferIn, BufferInSize, BufferOut, BufferOutSize);

		// It's the async way!
		return false;
		break;
	}
	case IOCTL_HID_SHUTDOWN:
	{
		std::lock_guard<std::mutex> lk(s_device_list_reply);
		if (deviceCommandAddress != 0){
			Memory::Write_U32(0xFFFFFFFF, Memory::Read_U32(deviceCommandAddress + 0x18));

			Memory::Write_U32(8, deviceCommandAddress);
			// IOS seems to write back the command that was responded to
			Memory::Write_U32(/*COMMAND_IOCTL*/ 6, deviceCommandAddress + 8);

			// Return value
			Memory::Write_U32(-1, deviceCommandAddress + 4);
			WII_IPC_HLE_Interface::EnqueReplyCallback(deviceCommandAddress);
			deviceCommandAddress = 0;
		}
		WARN_LOG(WII_IPC_HID, "HID::IOCtl(Shutdown) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			BufferIn, BufferInSize, BufferOut, BufferOutSize);
		break;
	}
	default:
	{
		WARN_LOG(WII_IPC_HID, "HID::IOCtl(0x%x) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			Parameter, BufferIn, BufferInSize, BufferOut, BufferOutSize);
		break;
	}
	}

	Memory::Write_U32(ReturnValue, _CommandAddress + 4);

	return true;
}


bool CWII_IPC_HLE_Device_hid::ClaimDevice(libusb_device_handle * dev)
{
	int ret = 0;
	if ((ret = libusb_kernel_driver_active(dev, 0)) == 1)
	{
		if ((ret = libusb_detach_kernel_driver(dev, 0)) && ret != LIBUSB_ERROR_NOT_SUPPORTED)
		{
			WARN_LOG(WII_IPC_HID, "libusb_detach_kernel_driver failed with error: %d", ret);
			return false;
		}
	}
	else if (ret != 0 && ret != LIBUSB_ERROR_NOT_SUPPORTED)
	{
		WARN_LOG(WII_IPC_HID, "libusb_kernel_driver_active error ret = %d", ret);
		return false;
	}

	if ((ret = libusb_claim_interface(dev, 0)))
	{
		WARN_LOG(WII_IPC_HID, "libusb_claim_interface failed with error: %d", ret);
		return false;
	}

	return true;
}

bool CWII_IPC_HLE_Device_hid::IOCtlV(u32 _CommandAddress)
{

	Dolphin_Debugger::PrintCallstack(LogTypes::WII_IPC_HID, LogTypes::LWARNING);
	u32 ReturnValue = 0;
	SIOCtlVBuffer CommandBuffer(_CommandAddress);

	WARN_LOG(WII_IPC_HID, "%s - IOCtlV:", GetDeviceName().c_str());
	WARN_LOG(WII_IPC_HID, "    Parameter: 0x%x", CommandBuffer.Parameter);
	WARN_LOG(WII_IPC_HID, "    NumberIn: 0x%08x", CommandBuffer.NumberInBuffer);
	WARN_LOG(WII_IPC_HID, "    NumberOut: 0x%08x", CommandBuffer.NumberPayloadBuffer);
	WARN_LOG(WII_IPC_HID, "    BufferVector: 0x%08x", CommandBuffer.BufferVector);
	WARN_LOG(WII_IPC_HID, "    PayloadAddr: 0x%08x", CommandBuffer.PayloadBuffer[0].m_Address);
	WARN_LOG(WII_IPC_HID, "    PayloadSize: 0x%08x", CommandBuffer.PayloadBuffer[0].m_Size);
	#if defined(_DEBUG) || defined(DEBUGFAST)
	DumpAsync(CommandBuffer.BufferVector, CommandBuffer.NumberInBuffer, CommandBuffer.NumberPayloadBuffer);
	#endif

	Memory::Write_U32(ReturnValue, _CommandAddress + 4);
	return true;
}



void CWII_IPC_HLE_Device_hid::ConvertDeviceToWii(WiiHIDDeviceDescriptor *dest, const struct libusb_device_descriptor *src)
{
	memcpy(dest,src,sizeof(WiiHIDDeviceDescriptor));
	dest->bcdUSB = Common::swap16(dest->bcdUSB);
	dest->idVendor = Common::swap16(dest->idVendor);
	dest->idProduct = Common::swap16(dest->idProduct);
	dest->bcdDevice = Common::swap16(dest->bcdDevice);
}

void CWII_IPC_HLE_Device_hid::ConvertConfigToWii(WiiHIDConfigDescriptor *dest, const struct libusb_config_descriptor *src)
{
	memcpy(dest,src,sizeof(WiiHIDConfigDescriptor));
	dest->wTotalLength = Common::swap16(dest->wTotalLength);
}

void CWII_IPC_HLE_Device_hid::ConvertInterfaceToWii(WiiHIDInterfaceDescriptor *dest, const struct libusb_interface_descriptor *src)
{
	memcpy(dest,src,sizeof(WiiHIDInterfaceDescriptor));
}

void CWII_IPC_HLE_Device_hid::ConvertEndpointToWii(WiiHIDEndpointDescriptor *dest, const struct libusb_endpoint_descriptor *src)
{
	memcpy(dest,src,sizeof(WiiHIDEndpointDescriptor));
	dest->wMaxPacketSize = Common::swap16(dest->wMaxPacketSize);
}

void CWII_IPC_HLE_Device_hid::FillOutDevices(u32 BufferOut, u32 BufferOutSize)
{
	#if defined(__LIBUSB__) || defined(_WIN32)
    const SCoreStartupParameter& _CoreParameter =
        SConfig::GetInstance().m_LocalCoreStartupParameter;

    if (!_CoreParameter.bUSBHIDEnabled)
    {
        // Pretend we have no devices.
        Memory::Write_U32(0xFFFFFFFF, BufferOut);
        return;
    }
	#endif

	static u16 check = 1;
	int OffsetBuffer = BufferOut;
	int OffsetStart = 0;
	//int OffsetDevice = 0;
	int d,c,ic,i,e; /* config, interface container, interface, endpoint  */

	libusb_device **list;
	//libusb_device *found = NULL;
	ssize_t cnt = libusb_get_device_list(NULL, &list);
	WARN_LOG(WII_IPC_HID, "Found %ld viable USB devices.", cnt);
	for (d = 0; d < cnt; d++)
	{
		libusb_device *device = list[d];
		struct libusb_device_descriptor desc;
		int dRet = libusb_get_device_descriptor (device, &desc);
		if (dRet)
		{
			// could not aquire the descriptor, no point in trying to use it.
			WARN_LOG(WII_IPC_HID, "libusb_get_device_descriptor failed with error: %d", dRet);
			continue;
		}
		OffsetStart = OffsetBuffer;
		OffsetBuffer += 4; // skip length for now, fill at end

		OffsetBuffer += 4; // skip devNum for now

		WiiHIDDeviceDescriptor wii_device;
		ConvertDeviceToWii(&wii_device, &desc);
		Memory::WriteBigEData((const u8*)&wii_device, OffsetBuffer, Align(wii_device.bLength, 4));
		OffsetBuffer += Align(wii_device.bLength, 4);
		bool deviceValid = true;

		for (c = 0; deviceValid && c < desc.bNumConfigurations; c++)
		{
			struct libusb_config_descriptor *config = NULL;
			int cRet = libusb_get_config_descriptor(device, c, &config);

			// do not try to use usb devices with more than one interface, games can crash
			if(cRet == 0 && config->bNumInterfaces <= MAX_HID_INTERFACES)
			{
				WiiHIDConfigDescriptor wii_config;
				ConvertConfigToWii(&wii_config, config);
				Memory::WriteBigEData((const u8*)&wii_config, OffsetBuffer, Align(wii_config.bLength, 4));
				OffsetBuffer += Align(wii_config.bLength, 4);

				for (ic = 0; ic < config->bNumInterfaces; ic++)
				{
					const struct libusb_interface *interfaceContainer = &config->interface[ic];
					for (i = 0; i < interfaceContainer->num_altsetting; i++)
					{
						const struct libusb_interface_descriptor *interface = &interfaceContainer->altsetting[i];

						WiiHIDInterfaceDescriptor wii_interface;
						ConvertInterfaceToWii(&wii_interface, interface);
						Memory::WriteBigEData((const u8*)&wii_interface, OffsetBuffer, Align(wii_interface.bLength, 4));
						OffsetBuffer += Align(wii_interface.bLength, 4);

						for (e = 0; e < interface->bNumEndpoints; e++)
						{
							const struct libusb_endpoint_descriptor *endpoint = &interface->endpoint[e];

							WiiHIDEndpointDescriptor wii_endpoint;
							ConvertEndpointToWii(&wii_endpoint, endpoint);
							Memory::WriteBigEData((const u8*)&wii_endpoint, OffsetBuffer, Align(wii_endpoint.bLength, 4));
							OffsetBuffer += Align(wii_endpoint.bLength, 4);

						} //endpoints
					} // interfaces
				} // interface containters
				libusb_free_config_descriptor(config);
				config = NULL;
			}
			else
			{
				if(cRet)
					WARN_LOG(WII_IPC_HID, "libusb_get_config_descriptor failed with: %d", cRet);
				deviceValid = false;
				OffsetBuffer = OffsetStart;
			}
		} // configs

		if (deviceValid)
		{
			Memory::Write_U32(OffsetBuffer-OffsetStart, OffsetStart); // fill in length

			int devNum = GetAvaiableDevNum(desc.idVendor,
											desc.idProduct,
											libusb_get_bus_number (device),
											libusb_get_device_address (device),
											check);
			if (devNum < 0 )
			{
				// too many devices to handle.
				ERROR_LOG(WII_IPC_HID, "Exhausted device list, you have way too many usb devices plugged in."
				"Or it might be our fault. Let us know at https://code.google.com/p/dolphin-emu/issues/entry?template=Defect%%20report");
				OffsetBuffer = OffsetStart;
				continue;
			}

			WARN_LOG(WII_IPC_HID, "Found device with Vendor: %X Product: %X Devnum: %d", desc.idVendor, desc.idProduct, devNum);

			Memory::Write_U32(devNum , OffsetStart+4); //write device num
		}
	}

	// Find devices that no longer exists and free them
	for (i=0; i<MAX_DEVICE_DEVNUM; i++)
	{
		u16 check_cur = (u16)(hidDeviceAliases[i] >> 48);
		if(hidDeviceAliases[i] != 0 && check_cur != check)
		{
			WARN_LOG(WII_IPC_HID, "Removing: device %d %hX %hX", i, check, check_cur);
			std::lock_guard<std::mutex> lk(s_open_devices);
			if (open_devices.find(i) != open_devices.end())
			{
				libusb_device_handle *handle = open_devices[i];
				libusb_close(handle);
				open_devices.erase(i);
			}
			hidDeviceAliases[i] = 0;
		}
	}
	check++;


	libusb_free_device_list(list, 1);

	Memory::Write_U32(0xFFFFFFFF, OffsetBuffer); // no more devices

}

int CWII_IPC_HLE_Device_hid::Align(int num, int alignment)
{
	return (num + (alignment-1)) & ~(alignment-1);
}


libusb_device_handle * CWII_IPC_HLE_Device_hid::GetDeviceByDevNum(u32 devNum)
{
	libusb_device **list;
	libusb_device_handle *handle = NULL;
	ssize_t cnt;

	if(devNum >= MAX_DEVICE_DEVNUM)
		return NULL;


	std::lock_guard<std::mutex> lk(s_open_devices);

	if (open_devices.find(devNum) != open_devices.end())
	{
		handle = open_devices[devNum];
		if(libusb_kernel_driver_active(handle, 0) != LIBUSB_ERROR_NO_DEVICE)
		{
			return handle;
		}
		else
		{
			libusb_close(handle);
			open_devices.erase(devNum);
		}
	}

	cnt = libusb_get_device_list(NULL, &list);

	if (cnt < 0)
		return NULL;

#ifdef _WIN32
	static bool has_warned_about_drivers = false;
#endif

	for (ssize_t i = 0; i < cnt; i++) {
		libusb_device *device = list[i];
		struct libusb_device_descriptor desc;
		int dRet = libusb_get_device_descriptor (device, &desc);
		u8 bus = libusb_get_bus_number (device);
		u8 port = libusb_get_device_address (device);
		u64 unique_id = ((u64)desc.idVendor << 32) | ((u64)desc.idProduct << 16) | ((u64)bus << 8) | (u64)port;
		if ((hidDeviceAliases[devNum] & HID_ID_MASK) == unique_id)
		{
			int ret = libusb_open(device, &handle);
			if (ret)
			{
				if (ret == LIBUSB_ERROR_ACCESS)
				{
					if( dRet )
					{
						ERROR_LOG(WII_IPC_HID, "Dolphin does not have access to this device: Bus %03d Device %03d: ID ????:???? (couldn't get id).",
								bus,
								port
						);
					}
					else{
						ERROR_LOG(WII_IPC_HID, "Dolphin does not have access to this device: Bus %03d Device %03d: ID %04X:%04X.",
								bus,
								port,
								desc.idVendor,
								desc.idProduct
						);
					}
				}
#ifdef _WIN32
				else if (ret == LIBUSB_ERROR_NOT_SUPPORTED)
				{
					if(!has_warned_about_drivers)
					{
						// Max of one warning.
						has_warned_about_drivers = true;
						WARN_LOG(WII_IPC_HID, "Please install the libusb drivers for the device %04X:%04X", desc.idVendor, desc.idProduct);
					}
				}
#endif
				else
				{
					ERROR_LOG(WII_IPC_HID, "libusb_open failed to open device with error = %d", ret);
				}
				continue;
			}


			if (!ClaimDevice(handle))
			{
				ERROR_LOG(WII_IPC_HID, "Could not claim the device for handle: %X", devNum);
				libusb_close(handle);
				continue;
			}

			open_devices[devNum] = handle;
			break;
		}
		else
		{
			handle = NULL;
		}
	}

	libusb_free_device_list(list, 1);


	return handle;
}


int CWII_IPC_HLE_Device_hid::GetAvaiableDevNum(u16 idVendor, u16 idProduct, u8 bus, u8 port, u16 check)
{
	int i;
	int pos = -1;
	u64 unique_id = ((u64)idVendor << 32) | ((u64)idProduct << 16) | ((u64)bus << 8) | (u64)port;
	for (i=0; i<MAX_DEVICE_DEVNUM; i++)
	{
		u64 id = hidDeviceAliases[i] & HID_ID_MASK;
		if(id == 0 && pos == -1)
		{
			pos = i;
		}
		else if (id == unique_id)
		{
			hidDeviceAliases[i] = id | ((u64)check << 48);
			return i;
		}
	}
	if(pos != -1)
	{
		hidDeviceAliases[pos] = unique_id | ((u64)check << 48);
		return pos;
	}
	return -1;
}
