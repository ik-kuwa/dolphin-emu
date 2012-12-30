// Copyright (C) 2003 Dolphin Project.

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2.0.

// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License 2.0 for more details.

// A copy of the GPL 2.0 should have been included with the program.
// If not, see http://www.gnu.org/licenses/

// Official SVN repository and contact information can be found at
// http://code.google.com/p/dolphin-emu/

#include "../Core.h"
#include "../Debugger/Debugger_SymbolMap.h"
#include "../HW/WII_IPC.h"
#include "WII_IPC_HLE.h"
#include "WII_IPC_HLE_Device_hid.h"
#include "errno.h"
#include <time.h>

#define MAX_DEVICE_DEVNUM 256
static u64 hidDeviceAliases[MAX_DEVICE_DEVNUM];

CWII_IPC_HLE_Device_hid::CWII_IPC_HLE_Device_hid(u32 _DeviceID, const std::string& _rDeviceName)
	: IWII_IPC_HLE_Device(_DeviceID, _rDeviceName)
{
	memset(hidDeviceAliases, 0, sizeof(hidDeviceAliases));
	libusb_init(NULL);
}

CWII_IPC_HLE_Device_hid::~CWII_IPC_HLE_Device_hid()
{
	for ( std::map<u32,libusb_device_handle*>::const_iterator iter = open_devices.begin(); iter != open_devices.end(); ++iter )
	{
		libusb_close(iter->second);
	}
	open_devices.clear();
	
	
	libusb_exit(NULL);
}

bool CWII_IPC_HLE_Device_hid::Open(u32 _CommandAddress, u32 _Mode)
{
	DEBUG_LOG(WII_IPC_HID, "HID::Open");
	m_Active = true;
	Memory::Write_U32(GetDeviceID(), _CommandAddress + 4);
	return true;
}

bool CWII_IPC_HLE_Device_hid::Close(u32 _CommandAddress, bool _bForce)
{
	DEBUG_LOG(WII_IPC_HID, "HID::Close");
	m_Active = false;
	if (!_bForce)
		Memory::Write_U32(0, _CommandAddress + 4);
	return true;
}

static u32  replyAddress = 0;
static bool hasRun = false;

u32 CWII_IPC_HLE_Device_hid::Update()
{
	static u16 timeToFill = 0; 
	u32 work_done = 0;
	
	if (timeToFill == 0)
	{
		if (replyAddress != 0){
			FillOutDevices(Memory::Read_U32(replyAddress + 0x18), Memory::Read_U32(replyAddress + 0x1C));
			
			Memory::Write_U32(8, replyAddress);
			// IOS seems to write back the command that was responded to
			Memory::Write_U32(/*COMMAND_IOCTL*/ 6, replyAddress + 8);
			
			// Return value
			Memory::Write_U32(0, replyAddress + 4);
			
			WII_IPC_HLE_Interface::EnqReply(replyAddress);
			replyAddress = 0;
			hasRun = false;
		}
	}
	
	timeToFill+=2;
	
	work_done = 1;
	
	//int ret = -4;
	//timeval tv;
	
	
	//libusb_handle_events_timeout_completed(NULL, &tv, NULL);
	/*
	std::list<_hidevent>::iterator ev = event_list.begin();
	while (ev != event_list.end()) {
		
		bool ev_finished = false;

		
		switch (ev->type)
		{
			case IOCTL_HID_INTERRUPT_OUT:
			case IOCTL_HID_INTERRUPT_IN:
			{
				ret = usb_reap_async_nocancel(ev->context, 0);
				if(ret >= 0)
				{
					Memory::Write_U32(ret, ev->enq_address + 4);
					WII_IPC_HLE_Interface::EnqReply(ev->enq_address);
					work_done = ev_finished = true;
				}
				break;
			}
		}
		
		if (ev_finished)
			event_list.erase(ev++);
		else
			ev++;
	}*/
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
		DEBUG_LOG(WII_IPC_HID, "HID::IOCtl(Get Attached) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			BufferIn, BufferInSize, BufferOut, BufferOutSize);
		//Memory::Write_U32(0xFFFFFFFF, BufferOut);
		if(!hasRun)
		{
			FillOutDevices(BufferOut, BufferOutSize);
			hasRun = true;
		}
		else
		{
			replyAddress = _CommandAddress;
			return false;
		}
		break;
	}
	case IOCTL_HID_OPEN:
	{
		DEBUG_LOG(WII_IPC_HID, "HID::IOCtl(Open) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			BufferIn, BufferInSize, BufferOut, BufferOutSize);

		//hid version, apparently
		ReturnValue = 0x40001;
		break;
	}
	case IOCTL_HID_SET_SUSPEND:
	{
		DEBUG_LOG(WII_IPC_HID, "HID::IOCtl(Set Suspend) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			BufferIn, BufferInSize, BufferOut, BufferOutSize);
		// not actually implemented in IOS
		ReturnValue = 0;
		break;
	}
	case IOCTL_HID_CANCEL_INTERRUPT:
	{
		DEBUG_LOG(WII_IPC_HID, "HID::IOCtl(Cancel Interrupt) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
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
		int ret = -1;
		
		u32 dev_num  = Memory::Read_U32(BufferIn+0x10);
		u8 requestType = Memory::Read_U8(BufferIn+0x14);
		u8 request = Memory::Read_U8(BufferIn+0x15);
		u16 value = Memory::Read_U16(BufferIn+0x16);
		u16 index = Memory::Read_U16(BufferIn+0x18);
		u16 size = Memory::Read_U16(BufferIn+0x1A);
		u32 data = Memory::Read_U32(BufferIn+0x1C);
		
		//DEBUG_LOG(WII_IPC_HID, "HID::IOCtl(Control)(%02X, %02X) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
		//	requestType, request, BufferIn, BufferInSize, BufferOut, BufferOutSize);

		libusb_device_handle * dev_handle = GetDeviceByDevNum(dev_num);
		
		if (dev_handle == NULL)
		{
			DEBUG_LOG(WII_IPC_HID, "Could not find handle: %X", dev_num);
			ReturnValue = -4;
			break;
		}
		
		if (!ClaimDevice(dev_handle))
		{
			DEBUG_LOG(WII_IPC_HID, "Could not claim the device for handle: %X", dev_num);
			ReturnValue = -4;
			break;
		}
		
		ret = libusb_control_transfer (dev_handle, requestType, request, value, index, (unsigned char*)Memory::GetPointer(data), size, 0);
		if(ret>=0)
		{
			ret += sizeof(libusb_control_setup);
			ReturnValue = ret;
		}
		else
		{
			ReturnValue = -4;
		}

		DEBUG_LOG(WII_IPC_HID, "HID::IOCtl(Control)(%02X, %02X) = %d (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			requestType, request, ReturnValue, BufferIn, BufferInSize, BufferOut, BufferOutSize);
		
		break;
	}
	case IOCTL_HID_INTERRUPT_OUT:
	case IOCTL_HID_INTERRUPT_IN:
	{
		
		int transfered = 0;
		int ret;
		u32 dev_num  = Memory::Read_U32(BufferIn+0x10);
		u32 endpoint = Memory::Read_U32(BufferIn+0x14);
		u32 length = Memory::Read_U32(BufferIn+0x18);

		u32 data = Memory::Read_U32(BufferIn+0x1C);
		//DEBUG_LOG(WII_IPC_HID, "HID::IOCtl(Interrupt %s)(%d,%d,%X) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
		//	Parameter == IOCTL_HID_INTERRUPT_IN ? "In" : "Out", endpoint, length, data, BufferIn, BufferInSize, BufferOut, BufferOutSize);
		libusb_device_handle * dev_handle = GetDeviceByDevNum(dev_num);
		
		if (dev_handle == NULL)
		{
			DEBUG_LOG(WII_IPC_HID, "Could not find handle: %X", dev_num);
			ReturnValue = -4;
			break;
		}
		
		if (!ClaimDevice(dev_handle))
		{
			DEBUG_LOG(WII_IPC_HID, "Could not claim the device for handle: %X", dev_num);
			ReturnValue = -4;
			break;
		}
		
		ret = libusb_interrupt_transfer(dev_handle, endpoint, (unsigned char*)Memory::GetPointer(data), length, &transfered, 40 );
		if(ret == 0)
		{
			ReturnValue = transfered;
		}
		else
		{
			ReturnValue = -4;
		}

		/*
			_hidevent ev;
			ev.enq_address = _CommandAddress;
			ev.type = Parameter;
			ev.context = context;
			event_list.push_back(ev);
			return false;
			*/
		DEBUG_LOG(WII_IPC_HID, "HID::IOCtl(Interrupt %s)(%d,%d,%X) = %d (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
			Parameter == IOCTL_HID_INTERRUPT_IN ? "In" : "Out", endpoint, length, data, ReturnValue, BufferIn, BufferInSize, BufferOut, BufferOutSize);

		break;
	}
	default:
	{
		DEBUG_LOG(WII_IPC_HID, "HID::IOCtl(0x%x) (BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
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
		//DEBUG_LOG(WII_IPC_HID, "Kernel has the interface, gtfo kernel!");
		if ((ret = libusb_detach_kernel_driver(dev, 0)) && ret != LIBUSB_ERROR_NOT_SUPPORTED)
		{
			DEBUG_LOG(WII_IPC_HID, "libusb_detach_kernel_driver failed with error: %d", ret);
			return false;
		}
	}
	else if (ret != 0 && ret != LIBUSB_ERROR_NOT_SUPPORTED)
	{
		DEBUG_LOG(WII_IPC_HID, "libusb_kernel_driver_active error ret = %d", ret);
		return false;
	}
	
	if ((ret = libusb_claim_interface(dev, 0)))
	{
		DEBUG_LOG(WII_IPC_HID, "libusb_claim_interface failed with error: %d", ret);
		return false;
	}
	
	return true;
}

bool CWII_IPC_HLE_Device_hid::IOCtlV(u32 _CommandAddress)
{
	
	Dolphin_Debugger::PrintCallstack(LogTypes::WII_IPC_HID, LogTypes::LWARNING);
	u32 ReturnValue = 0;
	SIOCtlVBuffer CommandBuffer(_CommandAddress);

	DEBUG_LOG(WII_IPC_HID, "%s - IOCtlV:", GetDeviceName().c_str());
	DEBUG_LOG(WII_IPC_HID, "    Parameter: 0x%x", CommandBuffer.Parameter);
	DEBUG_LOG(WII_IPC_HID, "    NumberIn: 0x%08x", CommandBuffer.NumberInBuffer);
	DEBUG_LOG(WII_IPC_HID, "    NumberOut: 0x%08x", CommandBuffer.NumberPayloadBuffer);
	DEBUG_LOG(WII_IPC_HID, "    BufferVector: 0x%08x", CommandBuffer.BufferVector);
	DEBUG_LOG(WII_IPC_HID, "    PayloadAddr: 0x%08x", CommandBuffer.PayloadBuffer[0].m_Address);
	DEBUG_LOG(WII_IPC_HID, "    PayloadSize: 0x%08x", CommandBuffer.PayloadBuffer[0].m_Size);
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
	int OffsetBuffer = BufferOut;
	int OffsetStart = 0;
	//int OffsetDevice = 0;
	int d,c,ic,i,e; /* config, interface container, interface, endpoint  */
	
	libusb_device **list;
	//libusb_device *found = NULL;
	ssize_t cnt = libusb_get_device_list(NULL, &list);
	DEBUG_LOG(WII_IPC_HID, "Hello plane???? %ld", cnt);
	for (d = 0; d < cnt; d++)
	{
		libusb_device *device = list[d];
		struct libusb_device_descriptor desc;
		int dRet = libusb_get_device_descriptor (device, &desc);
		
		//u32 devNum = (libusb_get_bus_number (device) << 8) | libusb_get_device_address (device);
		
		int devNum = GetAvaiableDevNum(desc.idVendor,
										desc.idProduct,
										libusb_get_bus_number (device),
										libusb_get_device_address (device));
		if (devNum < 0 )
		{
			DEBUG_LOG(WII_IPC_HID, "Too many snakes on thsi motherfucking plane.");
			// too many devices to handle.
			continue;
		}

		DEBUG_LOG(WII_IPC_HID, "Found device with Vendor: %d Product: %d Devnum: %d, Error: %d",desc.idVendor, desc.idProduct, devNum, dRet);
		OffsetStart = OffsetBuffer;
		OffsetBuffer += 4; // skip length for now, fill at end

		Memory::Write_U32( devNum , OffsetBuffer); //write device num
		OffsetBuffer += 4;

		WiiHIDDeviceDescriptor wii_device;
		ConvertDeviceToWii(&wii_device, &desc);
		Memory::WriteBigEData((const u8*)&wii_device, OffsetBuffer, Align(wii_device.bLength, 4));
		OffsetBuffer += Align(wii_device.bLength, 4);
		bool deviceValid = true;
    	for (c = 0; deviceValid && c < desc.bNumConfigurations; c++)
		{
			struct libusb_config_descriptor *config = NULL;
			int cRet = libusb_get_config_descriptor(device, c, &config);
			if(cRet == 0)
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
				DEBUG_LOG(WII_IPC_HID, "Could not open the device %d", cRet);
				deviceValid = false;
				OffsetBuffer = OffsetStart;
			}
		} // configs
		Memory::Write_U32(OffsetBuffer-OffsetStart, OffsetStart); // fill in length

	}
	
	libusb_free_device_list(list, 1);

	Memory::Write_U32(0xFFFFFFFF, OffsetBuffer); // no more devices
	
}
	
int CWII_IPC_HLE_Device_hid::Align(int num, int alignment)
{
	return (num + (alignment-1)) & ~(alignment-1);
}


libusb_device_handle * CWII_IPC_HLE_Device_hid::GetDeviceByDevNum(u32 devNum)
{
	u32 i;
	libusb_device **list;
	libusb_device_handle *handle = NULL;
	ssize_t cnt;
	
	if(devNum >= MAX_DEVICE_DEVNUM)
		return NULL;
	
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
	
	for (i = 0; i < cnt; i++) {
		libusb_device *device = list[i];
		struct libusb_device_descriptor desc;
		int dRet = libusb_get_device_descriptor (device, &desc);
		u8 bus = libusb_get_bus_number (device);
		u8 port = libusb_get_device_address (device);
		u64 unique_id = ((u64)desc.idVendor << 32) | (desc.idProduct << 16) |	(bus << 8) | port;
		if (hidDeviceAliases[devNum] == unique_id)
		{
			int ret = libusb_open(device, &handle);
			if (ret)
			{
				if (ret == LIBUSB_ERROR_ACCESS)
				{
					if( dRet )
					{
						DEBUG_LOG(WII_IPC_HID, "Dolphin does not have access to this device: Bus %03d Device %03d: ID ????:???? (couldn't get id).", 
								bus, 
								port
						);
					}
					else{
						DEBUG_LOG(WII_IPC_HID, "Dolphin does not have access to this device: Bus %03d Device %03d: ID %04X:%04X.", 
								bus, 
								port,
								desc.idVendor,
								desc.idProduct
						);
					}
				}
				else
				{
					DEBUG_LOG(WII_IPC_HID, "Failed to open device with error = %d", ret);
				}
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


int CWII_IPC_HLE_Device_hid::GetAvaiableDevNum(u16 idVendor, u16 idProduct, u8 bus, u8 port)
{
	int i;
	int pos = -1;
	u64 unique_id = ((u64)idVendor << 32) | (idProduct << 16) |	(bus << 8) | port;
	for (i=0; i< MAX_DEVICE_DEVNUM; i++)
	{
		u64 id = hidDeviceAliases[i];
		if(id == 0 && pos == -1)
		{
			pos = i;
		}
		else if (id == unique_id)
		{
			return i;
		}
	}
	if(pos != -1)
	{
		hidDeviceAliases[pos] = unique_id;
		return pos;
	}
	return -1;
}