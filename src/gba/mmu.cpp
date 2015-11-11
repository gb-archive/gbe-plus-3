// GB Enhanced+ Copyright Daniel Baxter 2014
// Licensed under the GPLv2
// See LICENSE.txt for full license text

// File : mmu.h
// Date : April 22, 2014
// Description : Game Boy Advance memory manager unit
//
// Handles reading and writing bytes to memory locations

#include "mmu.h"

/****** MMU Constructor ******/
AGB_MMU::AGB_MMU() 
{
	reset();
}

/****** MMU Deconstructor ******/
AGB_MMU::~AGB_MMU() 
{ 
	save_backup(config::save_file);
	memory_map.clear();
	std::cout<<"MMU::Shutdown\n"; 
}

/****** MMU Reset ******/
void AGB_MMU::reset()
{
	memory_map.clear();
	memory_map.resize(0x10000000, 0);

	eeprom.data.clear();
	eeprom.data.resize(0x200, 0);
	eeprom.size = 0x200;
	eeprom.size_lock = false;

	flash_ram.data.clear();
	flash_ram.bank = 0;
	flash_ram.current_command = 0;
	flash_ram.write_single_byte = false;
	flash_ram.switch_bank = false;
	flash_ram.grab_ids = false;
	flash_ram.next_write = false;

	flash_ram.data.resize(2);
	flash_ram.data[0].resize(0x10000, 0xFF);
	flash_ram.data[1].resize(0x10000, 0xFF);

	gpio.in_out = false;
	gpio.readable = false;
	gpio.input = gpio.output = 0;
	gpio.current_type = DISABLED;

	//HLE some post-boot registers
	if(!config::use_bios)
	{
		memory_map[DISPCNT] = 0x80;
		write_u16(0x4000134, 0x8000);
		write_u8(0x4000300, 0x1);
		write_u8(0x4000410, 0xFF);
		write_u32(0x4000800, 0xD000020);
	}

	bios_lock = false;

	write_u32(0x18, 0xEA000042);
	write_u32(0x128, 0xE92D500F);
	write_u32(0x12C, 0xE3A00301);
	write_u32(0x130, 0xE28FE000);
	write_u32(0x134, 0xE510F004);
	write_u32(0x138, 0xE8BD500F);
	write_u32(0x13C, 0xE25EF004);

	bios_lock = true;

	//Default memory access timings (4, 2)
	n_clock = 4;
	s_clock = 2;

	//Setup DMA info
	for(int x = 0; x < 4; x++)
	{
		dma[x].enable = false;
		dma[x].started = false;
		dma[x].start_address = 0;
		dma[x].original_start_address = 0;
		dma[x].destination_address = 0;
		dma[x].current_dma_position = 0;
		dma[x].word_count = 0;
		u8 word_type = 0;
		u16 control = 0;
		u8 dest_addr_ctrl = 0;
		u8 src_addr_ctrl = 0;
		u8 delay = 0;
	}

	current_save_type = NONE;

	g_pad = NULL;
	timer = NULL;

	std::cout<<"MMU::Initialized\n";
}

/****** Read byte from memory ******/
u8 AGB_MMU::read_u8(u32 address) const
{
	//Check for unused memory first
	if(address >= 0x10000000) { std::cout<<"Out of bounds read : 0x" << std::hex << address << "\n"; return 0; }

	//Read from FLASH RAM
	if(((current_save_type == FLASH_64) || (current_save_type == FLASH_128)) && (address >= 0xE000000) && (address <= 0xE00FFFF))
	{
		if((address == 0xE000000) && (current_save_type == FLASH_64) && (flash_ram.grab_ids)) { return 0x32; }
		else if((address == 0xE000000) && (current_save_type == FLASH_128) && (flash_ram.grab_ids)) { return 0xC2; }

		else if((address == 0xE000001) && (current_save_type == FLASH_64) && (flash_ram.grab_ids)) { return 0x1B; }
		else if((address == 0xE000001) && (current_save_type == FLASH_128) && (flash_ram.grab_ids)) { return 0x09; }

		return flash_ram.data[flash_ram.bank][(address & 0xFFFF)];
	}

	switch(address)
	{
		case TM0CNT_L:
			return (timer->at(0).counter & 0xFF);
			break;

		case TM0CNT_L+1:
			return (timer->at(0).counter >> 8);
			break;

		case TM1CNT_L:
			return (timer->at(1).counter & 0xFF);
			break;

		case TM1CNT_L+1:
			return (timer->at(1).counter >> 8);
			break;

		case TM2CNT_L:
			return (timer->at(2).counter & 0xFF);
			break;

		case TM2CNT_L+1:
			return (timer->at(2).counter >> 8);
			break;

		case TM3CNT_L:
			return (timer->at(3).counter & 0xFF);
			break;

		case TM3CNT_L+1:
			return (timer->at(3).counter >> 8);
			break;

		case KEYINPUT:
			return (g_pad->key_input & 0xFF);
			break;

		case KEYINPUT+1:
			return (g_pad->key_input >> 8);
			break;

		case WAVERAM0_L : return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4)]; break;
		case WAVERAM0_L+1: return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 1]; break;
		case WAVERAM0_H: return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 2]; break;
		case WAVERAM0_H+1: return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 3]; break;

		case WAVERAM1_L : return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 4]; break;
		case WAVERAM1_L+1: return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 5]; break;
		case WAVERAM1_H: return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 6]; break;
		case WAVERAM1_H+1: return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 7]; break;

		case WAVERAM2_L : return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 8]; break;
		case WAVERAM2_L+1: return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 9]; break;
		case WAVERAM2_H: return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 10]; break;
		case WAVERAM2_H+1: return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 11]; break;

		case WAVERAM3_L : return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 12]; break;
		case WAVERAM3_L+1: return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 13]; break;
		case WAVERAM3_H: return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 14]; break;
		case WAVERAM3_H+1: return apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 15]; break;

		//General Purpose I/O Data
		/*
		case GPIO_DATA:
			if((gpio.in_out) && (gpio.readable)) { return gpio.output; }
			else if((!gpio.in_out) && (gpio.readable)) { return gpio.input; }
			break;

		//General Purpose I/O Direction
		case GPIO_DIRECTION:
			if(gpio.readable) { return gpio.in_out; }
			break;

		//General Purpose I/O Control
		case GPIO_CNT:
			if(gpio.readable) { return gpio.readable; }
			break;
		*/
		
		default:
			return memory_map[address];
	}
}

/****** Read 2 bytes from memory ******/
u16 AGB_MMU::read_u16(u32 address) const
{
	return ((read_u8(address+1) << 8) | read_u8(address)); 
}

/****** Read 4 bytes from memory ******/
u32 AGB_MMU::read_u32(u32 address) const
{
	return ((read_u8(address+3) << 24) | (read_u8(address+2) << 16) | (read_u8(address+1) << 8) | read_u8(address));
}

/****** Reads 2 bytes from memory - No checks done on the read, used for known memory locations such as registers ******/
u16 AGB_MMU::read_u16_fast(u32 address) const
{
	return ((memory_map[address+1] << 8) | memory_map[address]);
}

/****** Reads 4 bytes from memory - No checks done on the read, used for known memory locations such as registers ******/
u32 AGB_MMU::read_u32_fast(u32 address) const
{
	return ((memory_map[address+3] << 24) | (memory_map[address+2] << 16) | (memory_map[address+1] << 8) | memory_map[address]);
}

/****** Write byte into memory ******/
void AGB_MMU::write_u8(u32 address, u8 value)
{
	//Check for unused memory first
	if(address >= 0x10000000) { std::cout<<"Out of bounds write : 0x" << std::hex << address << "\n"; return; }

	//BIOS is read-only, prevent any attempted writes
	else if((address <= 0x3FFF) && (bios_lock)) { return; }

	switch(address)
	{
		//Display Control
		case DISPCNT:
		case DISPCNT+1:
			memory_map[address] = value;
			lcd_stat->display_control = ((memory_map[DISPCNT+1] << 8) | memory_map[DISPCNT]);
			lcd_stat->bg_mode = lcd_stat->display_control & 0x7;
			lcd_stat->frame_base = (memory_map[DISPCNT] & 0x10) ? 0x600A000 : 0x6000000;
			lcd_stat->hblank_interval_free = (memory_map[DISPCNT] & 0x20) ? true : false;

			lcd_stat->window_enable[0] = (lcd_stat->display_control & 0x2000) ? true : false;
			lcd_stat->window_enable[1] = (lcd_stat->display_control & 0x4000) ? true : false;
			lcd_stat->obj_win_enable = (lcd_stat->display_control & 0x8000) ? true : false;

			lcd_stat->bg_enable[0] = (lcd_stat->display_control & 0x100) ? true : false;
			lcd_stat->bg_enable[1] = (lcd_stat->display_control & 0x200) ? true : false;
			lcd_stat->bg_enable[2] = (lcd_stat->display_control & 0x400) ? true : false;
			lcd_stat->bg_enable[3] = (lcd_stat->display_control & 0x800) ? true : false;
			break;

		//Display Status
		case DISPSTAT:
			{
				u8 read_only_bits = (memory_map[DISPSTAT] & 0x7);
				
				memory_map[address] = (value & ~0x7);
				memory_map[address] |= read_only_bits;
			}
 
			break;

		//BG0 Control
		case BG0CNT:
		case BG0CNT+1:
			memory_map[address] = value;
			lcd_stat->bg_priority[0] = memory_map[BG0CNT] & 0x3;
			lcd_stat->bg_control[0] = ((memory_map[BG0CNT+1] << 8) | memory_map[BG0CNT]);
			lcd_stat->bg_depth[0] = (lcd_stat->bg_control[0] & 0x80) ? 8 : 4;
			lcd_stat->bg_size[0] = lcd_stat->bg_control[0] >> 14;

			lcd_stat->bg_base_map_addr[0] = 0x6000000 + (0x800 * ((lcd_stat->bg_control[0] >> 8) & 0x1F));
			lcd_stat->bg_base_tile_addr[0] = 0x6000000 + (0x4000 * ((lcd_stat->bg_control[0] >> 2) & 0x3));

			switch(lcd_stat->bg_control[0] >> 14)
			{
				case 0x0: lcd_stat->mode_0_width[0] = 256; lcd_stat->mode_0_height[0] = 256; break;
				case 0x1: lcd_stat->mode_0_width[0] = 512; lcd_stat->mode_0_height[0] = 256; break;
				case 0x2: lcd_stat->mode_0_width[0] = 256; lcd_stat->mode_0_height[0] = 512; break;
				case 0x3: lcd_stat->mode_0_width[0] = 512; lcd_stat->mode_0_height[0] = 512; break;
			}

			break;

		//BG1 Control
		case BG1CNT:
		case BG1CNT+1:
			memory_map[address] = value;
			lcd_stat->bg_priority[1] = memory_map[BG1CNT] & 0x3;
			lcd_stat->bg_control[1] = ((memory_map[BG1CNT+1] << 8) | memory_map[BG1CNT]);
			lcd_stat->bg_depth[1] = (lcd_stat->bg_control[1] & 0x80) ? 8 : 4;
			lcd_stat->bg_size[1] = lcd_stat->bg_control[1] >> 14;

			lcd_stat->bg_base_map_addr[1] = 0x6000000 + (0x800 * ((lcd_stat->bg_control[1] >> 8) & 0x1F));
			lcd_stat->bg_base_tile_addr[1] = 0x6000000 + (0x4000 * ((lcd_stat->bg_control[1] >> 2) & 0x3));

			switch(lcd_stat->bg_control[1] >> 14)
			{
				case 0x0: lcd_stat->mode_0_width[1] = 256; lcd_stat->mode_0_height[1] = 256; break;
				case 0x1: lcd_stat->mode_0_width[1] = 512; lcd_stat->mode_0_height[1] = 256; break;
				case 0x2: lcd_stat->mode_0_width[1] = 256; lcd_stat->mode_0_height[1] = 512; break;
				case 0x3: lcd_stat->mode_0_width[1] = 512; lcd_stat->mode_0_height[1] = 512; break;
			}

			break;

		//BG2 Control
		case BG2CNT:
		case BG2CNT+1:
			memory_map[address] = value;
			lcd_stat->bg_priority[2] = memory_map[BG2CNT] & 0x3;
			lcd_stat->bg_control[2] = ((memory_map[BG2CNT+1] << 8) | memory_map[BG2CNT]);
			lcd_stat->bg_depth[2] = (lcd_stat->bg_control[2] & 0x80) ? 8 : 4;
			lcd_stat->bg_size[2] = lcd_stat->bg_control[2] >> 14;

			lcd_stat->bg_base_map_addr[2] = 0x6000000 + (0x800 * ((lcd_stat->bg_control[2] >> 8) & 0x1F));
			lcd_stat->bg_base_tile_addr[2] = 0x6000000 + (0x4000 * ((lcd_stat->bg_control[2] >> 2) & 0x3));

			lcd_stat->bg_params[0].overflow = (lcd_stat->bg_control[2] & 0x2000) ? true : false;

			switch(lcd_stat->bg_control[2] >> 14)
			{
				case 0x0: lcd_stat->mode_0_width[2] = 256; lcd_stat->mode_0_height[2] = 256; break;
				case 0x1: lcd_stat->mode_0_width[2] = 512; lcd_stat->mode_0_height[2] = 256; break;
				case 0x2: lcd_stat->mode_0_width[2] = 256; lcd_stat->mode_0_height[2] = 512; break;
				case 0x3: lcd_stat->mode_0_width[2] = 512; lcd_stat->mode_0_height[2] = 512; break;
			}

			break;

		//BG3 Control
		case BG3CNT:
		case BG3CNT+1:
			memory_map[address] = value;
			lcd_stat->bg_priority[3] = memory_map[BG3CNT] & 0x3;
			lcd_stat->bg_control[3] = ((memory_map[BG3CNT+1] << 8) | memory_map[BG3CNT]);
			lcd_stat->bg_depth[3] = (lcd_stat->bg_control[3] & 0x80) ? 8 : 4;
			lcd_stat->bg_size[3] = lcd_stat->bg_control[3] >> 14;

			lcd_stat->bg_base_map_addr[3] = 0x6000000 + (0x800 * ((lcd_stat->bg_control[3] >> 8) & 0x1F));
			lcd_stat->bg_base_tile_addr[3] = 0x6000000 + (0x4000 * ((lcd_stat->bg_control[3] >> 2) & 0x3));

			lcd_stat->bg_params[1].overflow = (lcd_stat->bg_control[3] & 0x2000) ? true : false;

			switch(lcd_stat->bg_control[3] >> 14)
			{
				case 0x0: lcd_stat->mode_0_width[3] = 256; lcd_stat->mode_0_height[3] = 256; break;
				case 0x1: lcd_stat->mode_0_width[3] = 512; lcd_stat->mode_0_height[3] = 256; break;
				case 0x2: lcd_stat->mode_0_width[3] = 256; lcd_stat->mode_0_height[3] = 512; break;
				case 0x3: lcd_stat->mode_0_width[3] = 512; lcd_stat->mode_0_height[3] = 512; break;
			}

			break;

		//BG0 Horizontal Offset
		case BG0HOFS:
		case BG0HOFS+1:
			memory_map[address] = value;
			lcd_stat->bg_offset_x[0] = ((memory_map[BG0HOFS+1] << 8) | memory_map[BG0HOFS]) & 0x1FF;
			break;

		//BG0 Vertical Offset
		case BG0VOFS:
		case BG0VOFS+1:
			memory_map[address] = value;
			lcd_stat->bg_offset_y[0] = ((memory_map[BG0VOFS+1] << 8) | memory_map[BG0VOFS]) & 0x1FF;
			break;

		//BG1 Horizontal Offset
		case BG1HOFS:
		case BG1HOFS+1:
			memory_map[address] = value;
			lcd_stat->bg_offset_x[1] = ((memory_map[BG1HOFS+1] << 8) | memory_map[BG1HOFS]) & 0x1FF;
			break;

		//BG1 Vertical Offset
		case BG1VOFS:
		case BG1VOFS+1:
			memory_map[address] = value;
			lcd_stat->bg_offset_y[1] = ((memory_map[BG1VOFS+1] << 8) | memory_map[BG1VOFS]) & 0x1FF;
			break;

		//BG2 Horizontal Offset
		case BG2HOFS:
		case BG2HOFS+1:
			memory_map[address] = value;
			lcd_stat->bg_offset_x[2] = ((memory_map[BG2HOFS+1] << 8) | memory_map[BG2HOFS]) & 0x1FF;
			break;

		//BG2 Vertical Offset
		case BG2VOFS:
		case BG2VOFS+1:
			memory_map[address] = value;
			lcd_stat->bg_offset_y[2] = ((memory_map[BG2VOFS+1] << 8) | memory_map[BG2VOFS]) & 0x1FF;
			break;

		//BG3 Horizontal Offset
		case BG3HOFS:
		case BG3HOFS+1:
			memory_map[address] = value;
			lcd_stat->bg_offset_x[3] = ((memory_map[BG3HOFS+1] << 8) | memory_map[BG3HOFS]) & 0x1FF;
			break;

		//BG3 Vertical Offset
		case BG3VOFS:
		case BG3VOFS+1:
			memory_map[address] = value;
			lcd_stat->bg_offset_y[3] = ((memory_map[BG3VOFS+1] << 8) | memory_map[BG3VOFS]) & 0x1FF;
			break;

		//BG2 Scale/Rotation Parameter A
		case BG2PA:
		case BG2PA+1:
			memory_map[address] = value;
			
			{
				u16 raw_value = ((memory_map[BG2PA+1] << 8) | memory_map[BG2PA]);
				
				//Note: The reference points are 8-bit signed 2's complement, not mentioned anywhere in docs...
				if(raw_value & 0x8000) 
				{ 
					u16 p = ((raw_value >> 8) - 1);
					p = ~p;
					p &= 0xFF;
					lcd_stat->bg_params[0].a = -1.0 * p;
				}
				else { lcd_stat->bg_params[0].a = (raw_value >> 8); }
				if((raw_value & 0xFF) != 0) { lcd_stat->bg_params[0].a += (raw_value & 0xFF) / 256.0; }
			}

			break;

		//BG2 Scale/Rotation Parameter B
		case BG2PB:
		case BG2PB+1:
			memory_map[address] = value;
			
			{
				u16 raw_value = ((memory_map[BG2PB+1] << 8) | memory_map[BG2PB]);
				
				//Note: The reference points are 8-bit signed 2's complement, not mentioned anywhere in docs...
				if(raw_value & 0x8000) 
				{ 
					u16 p = ((raw_value >> 8) - 1);
					p = ~p;
					p &= 0xFF;
					lcd_stat->bg_params[0].b = -1.0 * p;
				}
				else { lcd_stat->bg_params[0].b = (raw_value >> 8); }
				if((raw_value & 0xFF) != 0) { lcd_stat->bg_params[0].b += (raw_value & 0xFF) / 256.0; }
			}

			break;

		//BG2 Scale/Rotation Parameter C
		case BG2PC:
		case BG2PC+1:
			memory_map[address] = value;
			
			{
				u16 raw_value = ((memory_map[BG2PC+1] << 8) | memory_map[BG2PC]);
				
				//Note: The reference points are 8-bit signed 2's complement, not mentioned anywhere in docs...
				if(raw_value & 0x8000) 
				{ 
					u16 p = ((raw_value >> 8) - 1);
					p = ~p;
					p &= 0xFF;
					lcd_stat->bg_params[0].c = -1.0 * p;
				}
				else { lcd_stat->bg_params[0].c = (raw_value >> 8); }
				if((raw_value & 0xFF) != 0) { lcd_stat->bg_params[0].c += (raw_value & 0xFF) / 256.0; }
			}

			break;

		//BG2 Scale/Rotation Parameter D
		case BG2PD:
		case BG2PD+1:
			memory_map[address] = value;
			
			{
				u16 raw_value = ((memory_map[BG2PD+1] << 8) | memory_map[BG2PD]);
				
				//Note: The reference points are 8-bit signed 2's complement, not mentioned anywhere in docs...
				if(raw_value & 0x8000) 
				{ 
					u16 p = ((raw_value >> 8) - 1);
					p = ~p;
					p &= 0xFF;
					lcd_stat->bg_params[0].d = -1.0 * p;
				}
				else { lcd_stat->bg_params[0].d = (raw_value >> 8); }
				if((raw_value & 0xFF) != 0) { lcd_stat->bg_params[0].d += (raw_value & 0xFF) / 256.0; }
			}

			break;

		//BG2 Scale/Rotation X Reference
		case BG2X_L:
		case BG2X_L+1:
		case BG2X_L+2:
		case BG2X_L+3:
			memory_map[address] = value;

			{
				u32 x_raw = ((memory_map[BG2X_L+3] << 24) | (memory_map[BG2X_L+2] << 16) | (memory_map[BG2X_L+1] << 8) | (memory_map[BG2X_L]));

				//Note: The reference points are 19-bit signed 2's complement, not mentioned anywhere in docs...
				if(x_raw & 0x8000000) 
				{ 
					u32 x = ((x_raw >> 8) - 1);
					x = ~x;
					x &= 0x7FFFF;
					lcd_stat->bg_params[0].x_ref = -1.0 * x;
				}
				else { lcd_stat->bg_params[0].x_ref = (x_raw >> 8) & 0x7FFFF; }
				if((x_raw & 0xFF) != 0) { lcd_stat->bg_params[0].x_ref += (x_raw & 0xFF) / 256.0; }
			}

			break;

		//BG2 Scale/Rotation Y Reference
		case BG2Y_L:
		case BG2Y_L+1:
		case BG2Y_L+2:
		case BG2Y_L+3:
			memory_map[address] = value;

			{
				u32 y_raw = ((memory_map[BG2Y_L+3] << 24) | (memory_map[BG2Y_L+2] << 16) | (memory_map[BG2Y_L+1] << 8) | (memory_map[BG2Y_L]));

				//Note: The reference points are 19-bit signed 2's complement, not mentioned anywhere in docs...
				if(y_raw & 0x8000000) 
				{ 
					u32 y = ((y_raw >> 8) - 1);
					y = ~y;
					y &= 0x7FFFF;
					lcd_stat->bg_params[0].y_ref = -1.0 * y;
				}
				else { lcd_stat->bg_params[0].y_ref = (y_raw >> 8) & 0x7FFFF; }
				if((y_raw & 0xFF) != 0) { lcd_stat->bg_params[0].y_ref += (y_raw & 0xFF) / 256.0; }
			}

			break;

		//BG3 Scale/Rotation Parameter A
		case BG3PA:
		case BG3PA+1:
			memory_map[address] = value;
			
			{
				u16 raw_value = ((memory_map[BG3PA+1] << 8) | memory_map[BG3PA]);
				
				//Note: The reference points are 8-bit signed 2's complement, not mentioned anywhere in docs...
				if(raw_value & 0x8000) 
				{ 
					u16 p = ((raw_value >> 8) - 1);
					p = ~p;
					p &= 0xFF;
					lcd_stat->bg_params[1].a = -1.0 * p;
				}
				else { lcd_stat->bg_params[1].a = (raw_value >> 8); }
				if((raw_value & 0xFF) != 0) { lcd_stat->bg_params[1].a += (raw_value & 0xFF) / 256.0; }
			}

			break;

		//BG3 Scale/Rotation Parameter B
		case BG3PB:
		case BG3PB+1:
			memory_map[address] = value;
			
			{
				u16 raw_value = ((memory_map[BG3PB+1] << 8) | memory_map[BG3PB]);
				
				//Note: The reference points are 8-bit signed 2's complement, not mentioned anywhere in docs...
				if(raw_value & 0x8000) 
				{ 
					u16 p = ((raw_value >> 8) - 1);
					p = ~p;
					p &= 0xFF;
					lcd_stat->bg_params[1].b = -1.0 * p;
				}
				else { lcd_stat->bg_params[1].b = (raw_value >> 8); }
				if((raw_value & 0xFF) != 0) { lcd_stat->bg_params[1].b += (raw_value & 0xFF) / 256.0; }
			}

			break;

		//BG3 Scale/Rotation Parameter C
		case BG3PC:
		case BG3PC+1:
			memory_map[address] = value;
			
			{
				u16 raw_value = ((memory_map[BG3PC+1] << 8) | memory_map[BG3PC]);
				
				//Note: The reference points are 8-bit signed 2's complement, not mentioned anywhere in docs...
				if(raw_value & 0x8000) 
				{ 
					u16 p = ((raw_value >> 8) - 1);
					p = ~p;
					p &= 0xFF;
					lcd_stat->bg_params[1].c = -1.0 * p;
				}
				else { lcd_stat->bg_params[1].c = (raw_value >> 8); }
				if((raw_value & 0xFF) != 0) { lcd_stat->bg_params[1].c += (raw_value & 0xFF) / 256.0; }
			}

			break;

		//BG3 Scale/Rotation Parameter D
		case BG3PD:
		case BG3PD+1:
			memory_map[address] = value;
			
			{
				u16 raw_value = ((memory_map[BG3PD+1] << 8) | memory_map[BG3PD]);
				
				//Note: The reference points are 8-bit signed 2's complement, not mentioned anywhere in docs...
				if(raw_value & 0x8000) 
				{ 
					u16 p = ((raw_value >> 8) - 1);
					p = ~p;
					p &= 0xFF;
					lcd_stat->bg_params[1].d = -1.0 * p;
				}
				else { lcd_stat->bg_params[1].d = (raw_value >> 8); }
				if((raw_value & 0xFF) != 0) { lcd_stat->bg_params[1].d += (raw_value & 0xFF) / 256.0; }
			}

			break;

		//BG3 Scale/Rotation X Reference
		case BG3X_L:
		case BG3X_L+1:
		case BG3X_L+2:
		case BG3X_L+3:
			memory_map[address] = value;

			{
				u32 x_raw = ((memory_map[BG3X_L+3] << 24) | (memory_map[BG3X_L+2] << 16) | (memory_map[BG3X_L+1] << 8) | (memory_map[BG3X_L]));

				//Note: The reference points are 19-bit signed 2's complement, not mentioned anywhere in docs...
				if(x_raw & 0x8000000) 
				{ 
					u32 x = ((x_raw >> 8) - 1);
					x = ~x;
					x &= 0x7FFFF;
					lcd_stat->bg_params[1].x_ref = -1.0 * x;
				}
				else { lcd_stat->bg_params[1].x_ref = (x_raw >> 8) & 0x7FFFF; }
				if((x_raw & 0xFF) != 0) { lcd_stat->bg_params[1].x_ref += (x_raw & 0xFF) / 256.0; }
			}

			break;

		//BG3 Scale/Rotation Y Reference
		case BG3Y_L:
		case BG3Y_L+1:
		case BG3Y_L+2:
		case BG3Y_L+3:
			memory_map[address] = value;

			{
				u32 y_raw = ((memory_map[BG3Y_L+3] << 24) | (memory_map[BG3Y_L+2] << 16) | (memory_map[BG3Y_L+1] << 8) | (memory_map[BG3Y_L]));

				//Note: The reference points are 19-bit signed 2's complement, not mentioned anywhere in docs...
				if(y_raw & 0x8000000) 
				{ 
					u32 y = ((y_raw >> 8) - 1);
					y = ~y;
					y &= 0x7FFFF;
					lcd_stat->bg_params[1].y_ref = -1.0 * y;
				}
				else { lcd_stat->bg_params[1].y_ref = (y_raw >> 8) & 0x7FFFF; }
				if((y_raw & 0xFF) != 0) { lcd_stat->bg_params[1].y_ref += (y_raw & 0xFF) / 256.0; }
			}

			break;

		//Window 0 Horizontal Coordinates
		case WIN0H:
		case WIN0H+1:
			if(memory_map[address] == value) { return ; }

			memory_map[address] = value;
			lcd_stat->window_x1[0] = memory_map[WIN0H+1];
			lcd_stat->window_x2[0] = memory_map[WIN0H];

			if(lcd_stat->window_x2[0] > 240) { lcd_stat->window_x2[0] = 240; }
			if(lcd_stat->window_x2[0] > 0) { lcd_stat->window_x2[0]--; }

			//If the 2nd X coordinate is lower than the 1st, set both to 240
			if((lcd_stat->window_x2[0] < lcd_stat->window_x1[0]) && (memory_map[WIN0H] != 0)) { lcd_stat->window_x2[0] = lcd_stat->window_x1[0] = 240; }

			//However, if the 2nd X coordinate happens to be zero, this effectively enables the whole screen as the current window
			else if((lcd_stat->window_x2[0] < lcd_stat->window_x1[0]) && (memory_map[WIN0H] == 0)) { lcd_stat->window_x1[0] = 0; lcd_stat->window_x2[0] = 240; }

			//If the two X coordinates are the same, window should fail to draw
			//Set both to a pixel that the GBA cannot draw so the LCD won't render it
			else if(lcd_stat->window_x1[0] == lcd_stat->window_x2[0]) { lcd_stat->window_x1[0] = lcd_stat->window_x2[0] = 255; }
			break;

		//Window 1 Horizontal Coordinates
		case WIN1H:
		case WIN1H+1:
			if(memory_map[address] == value) { return ; }

			memory_map[address] = value;
			lcd_stat->window_x1[1] = memory_map[WIN1H+1];
			lcd_stat->window_x2[1] = memory_map[WIN1H];

			if(lcd_stat->window_x2[1] > 240) { lcd_stat->window_x2[1] = 240; }
			if(lcd_stat->window_x2[1] > 0) { lcd_stat->window_x2[1]--; }

			//If the 2nd X coordinate is lower than the 1st, set both to 240
			if((lcd_stat->window_x2[1] < lcd_stat->window_x1[1]) && (memory_map[WIN1H] != 0)) { lcd_stat->window_x2[1] = lcd_stat->window_x1[1] = 240; }

			//However, if the 2nd X coordinate happens to be zero, this effectively enables the whole screen as the current window
			else if((lcd_stat->window_x2[1] < lcd_stat->window_x1[1]) && (memory_map[WIN1H] == 0)) { lcd_stat->window_x1[1] = 0; lcd_stat->window_x2[1] = 240; }

			//If the two X coordinates are the same, window should fail to draw
			//Set both to a pixel that the GBA cannot draw so the LCD won't render it
			else if(lcd_stat->window_x1[1] == lcd_stat->window_x2[1]) { lcd_stat->window_x1[1] = lcd_stat->window_x2[1] = 255; }
			break;

		//Window 0 Vertical Coordinates
		case WIN0V:
		case WIN0V+1:
			if(memory_map[address] == value) { return ; }

			memory_map[address] = value;
			lcd_stat->window_y1[0] = memory_map[WIN0V+1];
			lcd_stat->window_y2[0] = memory_map[WIN0V];

			if(lcd_stat->window_y2[0] > 160) { lcd_stat->window_y2[0] = 160; }
			if(lcd_stat->window_y2[0] > 0) { lcd_stat->window_y2[0]--; }

			//If the 2nd Y coordinate is lower than the 1st, set both to 160
			if(lcd_stat->window_y2[0] < lcd_stat->window_y1[0]) { lcd_stat->window_y2[0] = lcd_stat->window_y1[0] = 160; }

			//If the two Y coordinates are the same, window should fail to draw
			//Set both to a pixel that the GBA cannot draw so the LCD won't render it
			else if(lcd_stat->window_y1[0] == lcd_stat->window_y2[0]) { lcd_stat->window_y1[0] = lcd_stat->window_y2[0] = 255; }
			break;

		//Window 1 Vertical Coordinates
		case WIN1V:
		case WIN1V+1:
			if(memory_map[address] == value) { return ; }

			memory_map[address] = value;
			lcd_stat->window_y1[1] = memory_map[WIN1V+1];
			lcd_stat->window_y2[1] = memory_map[WIN1V];

			if(lcd_stat->window_y2[1] > 160) { lcd_stat->window_y2[1] = 160; }
			if(lcd_stat->window_y2[1] > 0) { lcd_stat->window_y2[1]--; }

			//If the 2nd Y coordinate is lower than the 1st, set both to 160
			if(lcd_stat->window_y2[1] < lcd_stat->window_y1[1]) { lcd_stat->window_y2[1] = lcd_stat->window_y1[1] = 160; }

			//If the two Y coordinates are the same, window should fail to draw
			//Set both to a pixel that the GBA cannot draw so the LCD won't render it
			else if(lcd_stat->window_y1[1] == lcd_stat->window_y2[1]) { lcd_stat->window_y1[1] = lcd_stat->window_y2[1] = 255; }
			break;

		//Window 0 In Enable Flags
		case WININ:
			memory_map[address] = value;
			lcd_stat->window_in_enable[0][0] = (value & 0x1) ? true : false;
			lcd_stat->window_in_enable[1][0] = (value & 0x2) ? true : false;
			lcd_stat->window_in_enable[2][0] = (value & 0x4) ? true : false;
			lcd_stat->window_in_enable[3][0] = (value & 0x8) ? true : false;
			lcd_stat->window_in_enable[4][0] = (value & 0x10) ? true : false;
			lcd_stat->window_in_enable[5][0] = (value & 0x20) ? true : false;
			break;

		//Window 1 In Enable Flags
		case WININ+1:
			memory_map[address] = value;
			lcd_stat->window_in_enable[0][1] = (value & 0x1) ? true : false;
			lcd_stat->window_in_enable[1][1] = (value & 0x2) ? true : false;
			lcd_stat->window_in_enable[2][1] = (value & 0x4) ? true : false;
			lcd_stat->window_in_enable[3][1] = (value & 0x8) ? true : false;
			lcd_stat->window_in_enable[4][1] = (value & 0x10) ? true : false;
			lcd_stat->window_in_enable[5][1] = (value & 0x20) ? true : false;
			break;

		//Window 0 Out Enable Flags
		case WINOUT:
			memory_map[address] = value;
			lcd_stat->window_out_enable[0][0] = (value & 0x1) ? true : false;
			lcd_stat->window_out_enable[1][0] = (value & 0x2) ? true : false;
			lcd_stat->window_out_enable[2][0] = (value & 0x4) ? true : false;
			lcd_stat->window_out_enable[3][0] = (value & 0x8) ? true : false;
			lcd_stat->window_out_enable[4][0] = (value & 0x10) ? true : false;
			lcd_stat->window_out_enable[5][0] = (value & 0x20) ? true : false;
			break;

		//Window 1 Out Enable Flags
		case WINOUT+1:
			memory_map[address] = value;
			lcd_stat->window_out_enable[0][1] = (value & 0x1) ? true : false;
			lcd_stat->window_out_enable[1][1] = (value & 0x2) ? true : false;
			lcd_stat->window_out_enable[2][1] = (value & 0x4) ? true : false;
			lcd_stat->window_out_enable[3][1] = (value & 0x8) ? true : false;
			lcd_stat->window_out_enable[4][1] = (value & 0x10) ? true : false;
			lcd_stat->window_out_enable[5][1] = (value & 0x20) ? true : false;
			break;

		//Mosiac function
		case MOSIAC:
		case MOSIAC+1:
			memory_map[address] = value;

			lcd_stat->bg_mos_hsize = memory_map[MOSIAC] & 0xF;
			lcd_stat->bg_mos_vsize = memory_map[MOSIAC] >> 4;
			lcd_stat->obj_mos_hsize = memory_map[MOSIAC+1] & 0xF;
			lcd_stat->obj_mos_vsize = memory_map[MOSIAC+1] >> 4;

			if(lcd_stat->bg_mos_hsize) { lcd_stat->bg_mos_hsize--; }
			if(lcd_stat->bg_mos_vsize) { lcd_stat->bg_mos_vsize--; }
			if(lcd_stat->obj_mos_hsize) { lcd_stat->obj_mos_hsize--; }
			if(lcd_stat->obj_mos_vsize) { lcd_stat->obj_mos_vsize--; }

			break;

		//SFX Control
		case BLDCNT:
			memory_map[address] = value;
			lcd_stat->sfx_target[0][0] = (value & 0x1) ? true : false;
			lcd_stat->sfx_target[1][0] = (value & 0x2) ? true : false;
			lcd_stat->sfx_target[2][0] = (value & 0x4) ? true : false;
			lcd_stat->sfx_target[3][0] = (value & 0x8) ? true : false;
			lcd_stat->sfx_target[4][0] = (value & 0x10) ? true : false;
			lcd_stat->sfx_target[5][0] = (value & 0x20) ? true : false;

			switch(value >> 6)
			{
				case 0x0: lcd_stat->current_sfx_type = NORMAL; break;
				case 0x1: lcd_stat->current_sfx_type = ALPHA_BLEND; break;
				case 0x2: lcd_stat->current_sfx_type = BRIGHTNESS_UP; break;
				case 0x3: lcd_stat->current_sfx_type = BRIGHTNESS_DOWN; break;
			}			

			break;

		case BLDCNT+1:
			memory_map[address] = value;
			lcd_stat->sfx_target[0][1] = (value & 0x1) ? true : false;
			lcd_stat->sfx_target[1][1] = (value & 0x2) ? true : false;
			lcd_stat->sfx_target[2][1] = (value & 0x4) ? true : false;
			lcd_stat->sfx_target[3][1] = (value & 0x8) ? true : false;
			lcd_stat->sfx_target[4][1] = (value & 0x10) ? true : false;
			lcd_stat->sfx_target[5][1] = (value & 0x20) ? true : false;
			break;

		//SFX Alpha Control
		case BLDALPHA:
			if(memory_map[address] == value) { return; }
			
			memory_map[address] = value;
			if(value > 0xF) { value = 0x10; }
			lcd_stat->alpha_a_coef = (value & 0x1F) / 16.0;
			break;

		case BLDALPHA+1:
			if(memory_map[address] == value) { return; }
			
			memory_map[address] = value;
			if(value > 0xF) { value = 0x10; }
			lcd_stat->alpha_b_coef = (value & 0x1F) / 16.0;
			break;

		//SFX Brightness Control
		case BLDY:
			if(memory_map[address] == value) { return ; }

			memory_map[address] = value;
			if(value > 0xF) { value = 0x10; }
			lcd_stat->brightness_coef = (value & 0x1F) / 16.0;
			break;
		
		//Sound Channel 1 Control - Sweep Parameters
		case SND1CNT_L:
			memory_map[address] = value;
			apu_stat->channel[0].sweep_shift = value & 0x7;
			apu_stat->channel[0].sweep_direction = (value & 0x8) ? 1 : 0;
			apu_stat->channel[0].sweep_time = (value >> 4) & 0x7;
			break;

		//Sound Channel 1 Control - Duration, Duty Cycle, Envelope, Volume
		case SND1CNT_H:
		case SND1CNT_H+1:
			memory_map[address] = value;
			apu_stat->channel[0].duration = (memory_map[SND1CNT_H] & 0x3F);
			apu_stat->channel[0].duration = ((64 - apu_stat->channel[0].duration) / 256.0) * 1000.0;
			apu_stat->channel[0].duty_cycle = (memory_map[SND1CNT_H] >> 6) & 0x3;

			switch(apu_stat->channel[0].duty_cycle)
			{
				case 0x0: 
					apu_stat->channel[0].duty_cycle_start = 0;
					apu_stat->channel[0].duty_cycle_end = 1;
					break;

				case 0x1: 
					apu_stat->channel[0].duty_cycle_start = 0;
					apu_stat->channel[0].duty_cycle_end = 2;
					break;

				case 0x2: 
					apu_stat->channel[0].duty_cycle_start = 0;
					apu_stat->channel[0].duty_cycle_end = 4;
					break;

				case 0x3: 
					apu_stat->channel[0].duty_cycle_start = 0;
					apu_stat->channel[0].duty_cycle_end = 6;
					break;
			}

			apu_stat->channel[0].envelope_step = (memory_map[SND1CNT_H+1] & 0x7);
			apu_stat->channel[0].envelope_direction = (memory_map[SND1CNT_H+1] & 0x8) ? 1 : 0;
			apu_stat->channel[0].volume = (memory_map[SND1CNT_H+1] >> 4) & 0xF;
			break;

		//Sound Channel 1 Control - Length Flag, Frequency, Duty Cycle, Initial
		case SND1CNT_X:
		case SND1CNT_X+1:
			memory_map[address] = value;
			apu_stat->channel[0].raw_frequency = ((memory_map[SND1CNT_X+1] << 8) | memory_map[SND1CNT_X]) & 0x7FF;
			apu_stat->channel[0].output_frequency = (131072.0 / (2048 - apu_stat->channel[0].raw_frequency));

			switch(apu_stat->channel[0].duty_cycle)
			{
				case 0x0: 
					apu_stat->channel[0].duty_cycle_start = 0;
					apu_stat->channel[0].duty_cycle_end = 1;
					break;

				case 0x1: 
					apu_stat->channel[0].duty_cycle_start = 0;
					apu_stat->channel[0].duty_cycle_end = 2;
					break;

				case 0x2: 
					apu_stat->channel[0].duty_cycle_start = 0;
					apu_stat->channel[0].duty_cycle_end = 4;
					break;

				case 0x3: 
					apu_stat->channel[0].duty_cycle_start = 0;
					apu_stat->channel[0].duty_cycle_end = 6;
					break;
			}

			apu_stat->channel[0].length_flag = (memory_map[SND1CNT_X+1] & 0x40) ? true : false;
			apu_stat->channel[0].playing = (memory_map[SND1CNT_X+1] & 0x80) ? true : false;

			if(apu_stat->channel[0].volume == 0) { apu_stat->channel[0].playing = false; }

			if((address == SND1CNT_X+1) && (apu_stat->channel[0].playing)) 
			{
				apu_stat->channel[0].frequency_distance = 0;
				apu_stat->channel[0].sample_length = (apu_stat->channel[0].duration * apu_stat->sample_rate)/1000;
				apu_stat->channel[0].envelope_counter = 0;
				apu_stat->channel[0].sweep_counter = 0;
			}

			break;

		//Sound Channel 2 Control - Duration, Duty Cycle, Envelope, Volume
		case SND2CNT_L:
		case SND2CNT_L+1:
			memory_map[address] = value;
			apu_stat->channel[1].duration = (memory_map[SND2CNT_L] & 0x3F);
			apu_stat->channel[1].duration = ((64 - apu_stat->channel[1].duration) / 256.0) * 1000.0;
			apu_stat->channel[1].duty_cycle = (memory_map[SND2CNT_L] >> 6) & 0x3;

			switch(apu_stat->channel[1].duty_cycle)
			{
				case 0x0: 
					apu_stat->channel[1].duty_cycle_start = 0;
					apu_stat->channel[1].duty_cycle_end = 1;
					break;

				case 0x1: 
					apu_stat->channel[1].duty_cycle_start = 0;
					apu_stat->channel[1].duty_cycle_end = 2;
					break;

				case 0x2: 
					apu_stat->channel[1].duty_cycle_start = 0;
					apu_stat->channel[1].duty_cycle_end = 4;
					break;

				case 0x3: 
					apu_stat->channel[1].duty_cycle_start = 0;
					apu_stat->channel[1].duty_cycle_end = 6;
					break;
			}

			apu_stat->channel[1].envelope_step = (memory_map[SND2CNT_L+1] & 0x7);
			apu_stat->channel[1].envelope_direction = (memory_map[SND2CNT_L+1] & 0x8) ? 1 : 0;
			apu_stat->channel[1].volume = (memory_map[SND2CNT_L+1] >> 4) & 0xF;
			break;

		//Sound Channel 2 Control - Length Flag, Frequency, Duty Cycle, Initial
		case SND2CNT_H:
		case SND2CNT_H+1:
			memory_map[address] = value;
			apu_stat->channel[1].raw_frequency = ((memory_map[SND2CNT_H+1] << 8) | memory_map[SND2CNT_H]) & 0x7FF;
			apu_stat->channel[1].output_frequency = (131072.0 / (2048 - apu_stat->channel[1].raw_frequency));

			switch(apu_stat->channel[1].duty_cycle)
			{
				case 0x0: 
					apu_stat->channel[1].duty_cycle_start = 0;
					apu_stat->channel[1].duty_cycle_end = 1;
					break;

				case 0x1: 
					apu_stat->channel[1].duty_cycle_start = 0;
					apu_stat->channel[1].duty_cycle_end = 2;
					break;

				case 0x2: 
					apu_stat->channel[1].duty_cycle_start = 0;
					apu_stat->channel[1].duty_cycle_end = 4;
					break;

				case 0x3: 
					apu_stat->channel[1].duty_cycle_start = 0;
					apu_stat->channel[1].duty_cycle_end = 6;
					break;
			}

			apu_stat->channel[1].length_flag = (memory_map[SND2CNT_H+1] & 0x40) ? true : false;
			apu_stat->channel[1].playing = (memory_map[SND2CNT_H+1] & 0x80) ? true : false;

			if(apu_stat->channel[1].volume == 0) { apu_stat->channel[1].playing = false; }

			if((address == SND2CNT_H+1) && (apu_stat->channel[1].playing)) 
			{
				apu_stat->channel[1].frequency_distance = 0;
				apu_stat->channel[1].sample_length = (apu_stat->channel[1].duration * apu_stat->sample_rate)/1000;
				apu_stat->channel[1].envelope_counter = 0;
			}

			break;

		//Sound Channel 3 Control - Wave RAM Parameters + Channel Enable
		case SND3CNT_L:
			memory_map[address] = value;
			apu_stat->waveram_size = (memory_map[SND3CNT_L] & 0x20) ? 64 : 32;
			apu_stat->waveram_bank_play = (memory_map[SND3CNT_L] & 0x40) ? 1 : 0;
			apu_stat->waveram_bank_rw = (memory_map[SND3CNT_L] & 0x40) ? 0 : 1;
			apu_stat->channel[2].enable = (memory_map[SND3CNT_L] & 0x80) ? true : false;
			break;

		//Sound Channel 3 Control - Duration
		case SND3CNT_H:
			memory_map[address] = value;
			apu_stat->channel[2].duration = memory_map[SND3CNT_H];
			apu_stat->channel[2].duration = ((256 - apu_stat->channel[2].duration) / 256.0) * 1000.0;
			break;

		//Sound Channel 3 Control - Volume
		case SND3CNT_H+1:
			memory_map[address] = value;
			if(memory_map[SND3CNT_H+1] & 0x80) { apu_stat->channel[2].volume = 4; }
			else { apu_stat->channel[2].volume = (memory_map[SND3CNT_H+1] >> 5) & 0x3; }
			break;

		//Sound Channel 3 Control - Length Flag, Frequency, Initial
		case SND3CNT_X:
		case SND3CNT_X+1:
			memory_map[address] = value;
			apu_stat->channel[2].raw_frequency = ((memory_map[SND3CNT_X+1] << 8) | memory_map[SND3CNT_X]) & 0x7FF;
			apu_stat->channel[2].output_frequency = (131072.0 / (2048 - apu_stat->channel[2].raw_frequency)) / 2;

			apu_stat->channel[2].length_flag = (memory_map[SND3CNT_X+1] & 0x40) ? true : false;
			if((memory_map[SND3CNT_X+1] & 0x80) && (!apu_stat->channel[2].playing)) { apu_stat->channel[2].playing = true; }

			if((address == SND3CNT_X+1) && (apu_stat->channel[2].playing)) 
			{
				apu_stat->channel[2].frequency_distance = 0;
				apu_stat->channel[2].sample_length = (apu_stat->channel[2].duration * apu_stat->sample_rate)/1000;
			}

			break;

		//Sound Channel 4 Control - Duration, Envelope, Volume
		case SND4CNT_L:
		case SND4CNT_L+1:
			memory_map[address] = value;
			apu_stat->channel[3].duration = (memory_map[SND4CNT_L] & 0x3F);
			apu_stat->channel[3].duration = ((64 - apu_stat->channel[3].duration) / 256.0) * 1000.0;

			apu_stat->channel[3].envelope_step = (memory_map[SND4CNT_L+1] & 0x7);
			apu_stat->channel[3].envelope_direction = (memory_map[SND4CNT_L+1] & 0x8) ? 1 : 0;
			apu_stat->channel[3].volume = (memory_map[SND4CNT_L+1] >> 4) & 0xF;
			break;

		//Sound Channel 4 Control - Noise Parameters
		case SND4CNT_H:
			memory_map[address] = value;

			switch(memory_map[SND4CNT_H] & 0x7)
			{
				case 0x0: apu_stat->noise_dividing_ratio = 0.5; break;
				case 0x1: apu_stat->noise_dividing_ratio = 1.0; break;
				case 0x2: apu_stat->noise_dividing_ratio = 2.0; break;
				case 0x3: apu_stat->noise_dividing_ratio = 3.0; break;
				case 0x4: apu_stat->noise_dividing_ratio = 4.0; break;
				case 0x5: apu_stat->noise_dividing_ratio = 5.0; break;
				case 0x6: apu_stat->noise_dividing_ratio = 6.0; break;
				case 0x7: apu_stat->noise_dividing_ratio = 7.0; break;
			}

			apu_stat->noise_stages = (memory_map[SND4CNT_H] & 0x8) ? 7 : 15;
			apu_stat->noise_prescalar = 2 << (memory_map[SND4CNT_H] >> 4);
			apu_stat->channel[3].output_frequency = (524288 / apu_stat->noise_dividing_ratio) / apu_stat->noise_prescalar;
			break;

		//Sound Channel 4 - Length Flag + Initial
		case SND4CNT_H+1:
			memory_map[address] = value;
			apu_stat->channel[3].length_flag = (memory_map[SND4CNT_H+1] & 0x40) ? true : false;
			apu_stat->channel[3].playing = (memory_map[SND4CNT_H+1] & 0x80) ? true : false;

			if(apu_stat->channel[3].volume == 0) { apu_stat->channel[3].playing = false; }

			if((address == SND4CNT_H+1) && (apu_stat->channel[3].playing)) 
			{
				apu_stat->channel[3].frequency_distance = 0;
				apu_stat->channel[3].sample_length = (apu_stat->channel[3].duration * apu_stat->sample_rate)/1000;
				apu_stat->channel[3].envelope_counter = 0;
			}

			break;

		//General Sound Control - Volumes
		case SNDCNT_L:
			memory_map[address] = value;

			switch(value & 0x7)
			{
				case 0x0: apu_stat->channel_right_volume = 4369 * 0.125; break;
				case 0x1: apu_stat->channel_right_volume = 4369 * 0.25; break;
				case 0x2: apu_stat->channel_right_volume = 4369 * 0.375; break;
				case 0x3: apu_stat->channel_right_volume = 4369 * 0.5; break;
				case 0x4: apu_stat->channel_right_volume = 4369 * 0.625; break;
				case 0x5: apu_stat->channel_right_volume = 4369 * 0.75; break;
				case 0x6: apu_stat->channel_right_volume = 4369 * 0.875; break;
				case 0x7: apu_stat->channel_right_volume = 4369; break;
			}

			switch((value >> 4) & 0x7)
			{
				case 0x0: apu_stat->channel_left_volume = 4369 * 0.125; break;
				case 0x1: apu_stat->channel_left_volume = 4369 * 0.25; break;
				case 0x2: apu_stat->channel_left_volume = 4369 * 0.375; break;
				case 0x3: apu_stat->channel_left_volume = 4369 * 0.5; break;
				case 0x4: apu_stat->channel_left_volume = 4369 * 0.625; break;
				case 0x5: apu_stat->channel_left_volume = 4369 * 0.75; break;
				case 0x6: apu_stat->channel_left_volume = 4369 * 0.875; break;
				case 0x7: apu_stat->channel_left_volume = 4369; break;
			}

			break;

		//General Sound Control - Enabled Sound Channels
		case SNDCNT_L+1:
			memory_map[address] = value;
			apu_stat->channel[0].right_enable = (value & 0x1) ? true : false;
			apu_stat->channel[1].right_enable = (value & 0x2) ? true : false;
			apu_stat->channel[2].right_enable = (value & 0x4) ? true : false;
			apu_stat->channel[3].right_enable = (value & 0x8) ? true : false;
			apu_stat->channel[0].left_enable = (value & 0x10) ? true : false;
			apu_stat->channel[1].left_enable = (value & 0x20) ? true : false;
			apu_stat->channel[2].left_enable = (value & 0x40) ? true : false;
			apu_stat->channel[3].left_enable = (value & 0x80) ? true : false;
			break;

		//General Sound Control - Master Volumes + Enabled DMA Channels
		case SNDCNT_H:
		case SNDCNT_H+1:
			memory_map[address] = value;

			switch(memory_map[SNDCNT_H] & 0x3)
			{
				case 0x0: apu_stat->channel_master_volume = (config::volume >> 4); break;
				case 0x1: apu_stat->channel_master_volume = (config::volume >> 3); break;
				case 0x2: apu_stat->channel_master_volume = (config::volume >> 2); break;
				case 0x3: std::cout<<"MMU::Setting prohibited Sound Channel master volume - 0x3\n"; break;
			}

			apu_stat->dma[0].master_volume = (memory_map[SNDCNT_H] & 0x4) ? config::volume : (config::volume >> 1);
			apu_stat->dma[1].master_volume = (memory_map[SNDCNT_H] & 0x8) ? config::volume : (config::volume >> 1);

			apu_stat->dma[0].right_enable = (memory_map[SNDCNT_H+1] & 0x1) ? true : false;
			apu_stat->dma[0].left_enable = (memory_map[SNDCNT_H+1] & 0x2) ? true : false;
			apu_stat->dma[0].timer = (memory_map[SNDCNT_H+1] & 0x4) ? 1 : 0;

			apu_stat->dma[1].right_enable = (memory_map[SNDCNT_H+1] & 0x10) ? true : false;
			apu_stat->dma[1].left_enable = (memory_map[SNDCNT_H+1] & 0x20) ? true : false;
			apu_stat->dma[1].timer = (memory_map[SNDCNT_H+1] & 0x40) ? 1 : 0;
			break;
			

		//Wave RAM
		case WAVERAM0_L : apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4)] = value; break;
		case WAVERAM0_L+1: apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 1] = value; break;
		case WAVERAM0_H: apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 2] = value; break;
		case WAVERAM0_H+1: apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 3] = value; break;

		case WAVERAM1_L : apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 4] = value; break;
		case WAVERAM1_L+1: apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 5] = value; break;
		case WAVERAM1_H: apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 6] = value; break;
		case WAVERAM1_H+1: apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 7] = value; break;

		case WAVERAM2_L : apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 8] = value; break;
		case WAVERAM2_L+1: apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 9] = value; break;
		case WAVERAM2_H: apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 10] = value; break;
		case WAVERAM2_H+1: apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 11] = value; break;

		case WAVERAM3_L : apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 12] = value; break;
		case WAVERAM3_L+1: apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 13] = value; break;
		case WAVERAM3_H: apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 14] = value; break;
		case WAVERAM3_H+1: apu_stat->waveram_data[(apu_stat->waveram_bank_rw << 4) + 15] = value; break;

		case REG_IF:
		case REG_IF+1:
			memory_map[address] &= ~value;
			break;

		//DMA0 Start Address
		case DMA0SAD:
		case DMA0SAD+1:
		case DMA0SAD+2:
		case DMA0SAD+3:
			memory_map[address] = value;
			dma[0].start_address = ((memory_map[DMA0SAD+3] << 24) | (memory_map[DMA0SAD+2] << 16) | (memory_map[DMA0SAD+1] << 8) | memory_map[DMA0SAD]) & 0x7FFFFFF;
			break;

		//DMA0 Destination Address
		case DMA0DAD:
		case DMA0DAD+1:
		case DMA0DAD+2:
		case DMA0DAD+3:
			memory_map[address] = value;
			dma[0].destination_address = ((memory_map[DMA0DAD+3] << 24) | (memory_map[DMA0DAD+2] << 16) | (memory_map[DMA0DAD+1] << 8) | memory_map[DMA0DAD]) & 0x7FFFFFF;
			break;

		//DMA0 Control
		case DMA0CNT_H:
		case DMA0CNT_H+1:
			memory_map[address] = value;
			dma[0].control = ((memory_map[DMA0CNT_H+1] << 8) | memory_map[DMA0CNT_H]);
			dma[0].dest_addr_ctrl = (dma[0].control >> 5) & 0x3;
			dma[0].src_addr_ctrl = (dma[0].control >> 7) & 0x3;
			
			dma[0].enable = true;
			dma[0].started = false;
			dma[0].delay = 2;
			break;

		//DMA1 Start Address
		case DMA1SAD:
		case DMA1SAD+1:
		case DMA1SAD+2:
		case DMA1SAD+3:
			memory_map[address] = value;
			dma[1].start_address = ((memory_map[DMA1SAD+3] << 24) | (memory_map[DMA1SAD+2] << 16) | (memory_map[DMA1SAD+1] << 8) | memory_map[DMA1SAD]) & 0xFFFFFFF;
			dma[1].original_start_address = dma[1].start_address;
			break;

		//DMA1 Destination Address
		case DMA1DAD:
		case DMA1DAD+1:
		case DMA1DAD+2:
		case DMA1DAD+3:
			memory_map[address] = value;
			dma[1].destination_address = ((memory_map[DMA1DAD+3] << 24) | (memory_map[DMA1DAD+2] << 16) | (memory_map[DMA1DAD+1] << 8) | memory_map[DMA1DAD]) & 0x7FFFFFF;
			break;

		//DMA1 Control
		case DMA1CNT_H:
		case DMA1CNT_H+1:
			memory_map[address] = value;
			dma[1].control = ((memory_map[DMA1CNT_H+1] << 8) | memory_map[DMA1CNT_H]);
			dma[1].dest_addr_ctrl = (dma[1].control >> 5) & 0x3;
			dma[1].src_addr_ctrl = (dma[1].control >> 7) & 0x3;

			if((dma[1].control & 0x200) == 0) { dma[1].start_address = dma[1].original_start_address; }

			dma[1].enable = true;
			dma[1].started = false;
			dma[1].delay = 2;
			break;

		//DMA2 Start Address
		case DMA2SAD:
		case DMA2SAD+1:
		case DMA2SAD+2:
		case DMA2SAD+3:
			memory_map[address] = value;
			dma[2].start_address = ((memory_map[DMA2SAD+3] << 24) | (memory_map[DMA2SAD+2] << 16) | (memory_map[DMA2SAD+1] << 8) | memory_map[DMA2SAD]) & 0xFFFFFFF;
			dma[2].original_start_address = dma[2].start_address;
			break;

		//DMA2 Destination Address
		case DMA2DAD:
		case DMA2DAD+1:
		case DMA2DAD+2:
		case DMA2DAD+3:
			memory_map[address] = value;
			dma[2].destination_address = ((memory_map[DMA2DAD+3] << 24) | (memory_map[DMA2DAD+2] << 16) | (memory_map[DMA2DAD+1] << 8) | memory_map[DMA2DAD]) & 0x7FFFFFF;
			break;

		//DMA2 Control
		case DMA2CNT_H:
		case DMA2CNT_H+1:
			memory_map[address] = value;
			dma[2].control = ((memory_map[DMA2CNT_H+1] << 8) | memory_map[DMA2CNT_H]);
			dma[2].dest_addr_ctrl = (dma[2].control >> 5) & 0x3;
			dma[2].src_addr_ctrl = (dma[2].control >> 7) & 0x3;

			if((dma[2].control & 0x200) == 0) { dma[2].start_address = dma[2].original_start_address; }

			dma[2].enable = true;
			dma[2].started = false;
			dma[2].delay = 2;
			break;

		//DMA3 Start Address
		case DMA3SAD:
		case DMA3SAD+1:
		case DMA3SAD+2:
		case DMA3SAD+3:
			memory_map[address] = value;
			dma[3].start_address = ((memory_map[DMA3SAD+3] << 24) | (memory_map[DMA3SAD+2] << 16) | (memory_map[DMA3SAD+1] << 8) | memory_map[DMA3SAD]) & 0xFFFFFFF;
			break;

		//DMA3 Destination Address
		case DMA3DAD:
		case DMA3DAD+1:
		case DMA3DAD+2:
		case DMA3DAD+3:
			memory_map[address] = value;
			dma[3].destination_address = ((memory_map[DMA3DAD+3] << 24) | (memory_map[DMA3DAD+2] << 16) | (memory_map[DMA3DAD+1] << 8) | memory_map[DMA3DAD]) & 0xFFFFFFF;
			break;

		//DMA3 Control
		case DMA3CNT_H:
		case DMA3CNT_H+1:
			memory_map[address] = value;
			dma[3].control = ((memory_map[DMA3CNT_H+1] << 8) | memory_map[DMA3CNT_H]);
			dma[3].dest_addr_ctrl = (dma[3].control >> 5) & 0x3;
			dma[3].src_addr_ctrl = (dma[3].control >> 7) & 0x3;

			dma[3].enable = true;
			dma[3].started = false;
			dma[3].delay = 2;
			break;

		case KEYINPUT: break;

		//Timer 0 Reload Value
		case TM0CNT_L:
		case TM0CNT_L+1:
			memory_map[address] = value;
			timer->at(0).reload_value = ((memory_map[TM0CNT_L+1] << 8) | memory_map[TM0CNT_L]);
			if((apu_stat->dma[0].timer == 0) && (timer->at(0).reload_value != 0xFFFF)) { apu_stat->dma[0].output_frequency = (1 << 24) / (0xFFFF - timer->at(0).reload_value); }
			if((apu_stat->dma[1].timer == 0) && (timer->at(0).reload_value != 0xFFFF)) { apu_stat->dma[1].output_frequency = (1 << 24) / (0xFFFF - timer->at(0).reload_value); }
			break;

		//Timer 1 Reload Value
		case TM1CNT_L:
		case TM1CNT_L+1:
			memory_map[address] = value;
			timer->at(1).reload_value = ((memory_map[TM1CNT_L+1] << 8) | memory_map[TM1CNT_L]);
			if((apu_stat->dma[0].timer == 1) && (timer->at(1).reload_value != 0xFFFF)) { apu_stat->dma[0].output_frequency = (1 << 24) / (0xFFFF - timer->at(1).reload_value); }
			if((apu_stat->dma[1].timer == 1) && (timer->at(1).reload_value != 0xFFFF)) { apu_stat->dma[1].output_frequency = (1 << 24) / (0xFFFF - timer->at(1).reload_value); }
			break;

		//Timer 2 Reload Value
		case TM2CNT_L:
		case TM2CNT_L+1:
			memory_map[address] = value;
			timer->at(2).reload_value = ((memory_map[TM2CNT_L+1] << 8) | memory_map[TM2CNT_L]);
			break;

		//Timer 3 Reload Value
		case TM3CNT_L:
		case TM3CNT_L+1:
			memory_map[address] = value;
			timer->at(3).reload_value = ((memory_map[TM3CNT_L+1] << 8) | memory_map[TM3CNT_L]);
			break;

		//Timer 0 Control
		case TM0CNT_H:
		case TM0CNT_H+1:
			{
				bool prev_enable = (memory_map[TM0CNT_H] & 0x80) ?  true : false;
				memory_map[address] = value;

				timer->at(0).enable = (memory_map[TM0CNT_H] & 0x80) ?  true : false;
				timer->at(0).interrupt = (memory_map[TM0CNT_H] & 0x40) ? true : false;
				if((timer->at(0).enable) && (!prev_enable)) { timer->at(0).counter = timer->at(0).reload_value; }
			}

			switch(memory_map[TM0CNT_H] & 0x3)
			{
				case 0x0: timer->at(0).prescalar = 1; break;
				case 0x1: timer->at(0).prescalar = 64; break;
				case 0x2: timer->at(0).prescalar = 256; break;
				case 0x3: timer->at(0).prescalar = 1024; break;
			}

			break;

		//Timer 1 Control
		case TM1CNT_H:
		case TM1CNT_H+1:
			{
				bool prev_enable = (memory_map[TM1CNT_H] & 0x80) ?  true : false;
				memory_map[address] = value;

				timer->at(1).count_up = (memory_map[TM1CNT_H] & 0x4) ? true : false;
				timer->at(1).enable = (memory_map[TM1CNT_H] & 0x80) ?  true : false;
				timer->at(1).interrupt = (memory_map[TM1CNT_H] & 0x40) ? true : false;
				if((timer->at(1).enable) && (!prev_enable)) { timer->at(1).counter = timer->at(1).reload_value; }
			}

			switch(memory_map[TM1CNT_H] & 0x3)
			{
				case 0x0: timer->at(1).prescalar = 1; break;
				case 0x1: timer->at(1).prescalar = 64; break;
				case 0x2: timer->at(1).prescalar = 256; break;
				case 0x3: timer->at(1).prescalar = 1024; break;
			}

			if(timer->at(1).count_up) { timer->at(1).prescalar = 1; }

			break;

		//Timer 2 Control
		case TM2CNT_H:
		case TM2CNT_H+1:
			{
				bool prev_enable = (memory_map[TM2CNT_H] & 0x80) ?  true : false;
				memory_map[address] = value;

				timer->at(2).count_up = (memory_map[TM2CNT_H] & 0x4) ? true : false;
				timer->at(2).enable = (memory_map[TM2CNT_H] & 0x80) ?  true : false;
				timer->at(2).interrupt = (memory_map[TM2CNT_H] & 0x40) ? true : false;
				if((timer->at(2).enable) && (!prev_enable)) { timer->at(2).counter = timer->at(2).reload_value; }
			}

			switch(memory_map[TM2CNT_H] & 0x3)
			{
				case 0x0: timer->at(2).prescalar = 1; break;
				case 0x1: timer->at(2).prescalar = 64; break;
				case 0x2: timer->at(2).prescalar = 256; break;
				case 0x3: timer->at(2).prescalar = 1024; break;
			}

			if(timer->at(2).count_up) { timer->at(2).prescalar = 1; }

			break;

		//Timer 3 Control
		case TM3CNT_H:
		case TM3CNT_H+1:
			{
				bool prev_enable = (memory_map[TM3CNT_H] & 0x80) ?  true : false;
				memory_map[address] = value;

				timer->at(3).count_up = (memory_map[TM3CNT_H] & 0x4) ? true : false;
				timer->at(3).enable = (memory_map[TM3CNT_H] & 0x80) ?  true : false;
				timer->at(3).interrupt = (memory_map[TM3CNT_H] & 0x40) ? true : false;
				if((timer->at(3).enable) && (!prev_enable)) { timer->at(3).counter = timer->at(3).reload_value; }
			}

			switch(memory_map[TM3CNT_H] & 0x3)
			{
				case 0x0: timer->at(3).prescalar = 1; break;
				case 0x1: timer->at(3).prescalar = 64; break;
				case 0x2: timer->at(3).prescalar = 256; break;
				case 0x3: timer->at(3).prescalar = 1024; break;
			}

			if(timer->at(3).count_up) { timer->at(3).prescalar = 1; }

			break;

		//Wait State Control
		case WAITCNT:
		case WAITCNT+1:
			{
				memory_map[address] = value;
				u16 wait_control = read_u16(WAITCNT);

				//Determine first access cycles (Non-Sequential)
				switch((wait_control >> 2) & 0x3)
				{
					case 0x0: n_clock = 4; break;
					case 0x1: n_clock = 3; break;
					case 0x2: n_clock = 2; break;
					case 0x3: n_clock = 8; break;
				}

				//Determine second access cycles (Sequential)
				switch((wait_control >> 4) & 0x1)
				{
					case 0x0: s_clock = 2; break;
					case 0x1: s_clock = 1; break;
				}
			}

			break;

		//General Purpose I/O Data
		/*
		case GPIO_DATA:
			memory_map[address] = (value & 0x7);

			if(gpio.in_out) { gpio.output = (value & 0x7); }
			else { gpio.input = (value & 0x7); }
			break;

		//General Purpose I/O Direction
		case GPIO_DIRECTION:
			memory_map[address] = value & 0x1;
			gpio.in_out = (memory_map[address] & 0x1) ? true : false;
			break;

		//General Purpose I/O Control
		case GPIO_CNT:
			memory_map[address] = value & 0x1;
			gpio.readable = (memory_map[address] & 0x1) ? true : false;
			break;
		*/

		case FLASH_RAM_CMD0:
			memory_map[address] = value;

			if((current_save_type == FLASH_64) || (current_save_type == FLASH_128))
			{
				//1st byte of the command
				if((flash_ram.current_command == 0) && (value == 0xAA)) { flash_ram.current_command++; }

				//3rd byte of the command, execute command
				else if(flash_ram.current_command == 2)
				{
					switch(value)
					{
						//FLASH erase chip
						case 0x10: 
							flash_erase_chip();
							flash_ram.current_command = 0;
							break;

						//FLASH erase command
						case 0x80:
							flash_ram.current_command = 0;
							break;			

						//FLASH ID start
						case 0x90:
							flash_ram.grab_ids = true;
							flash_ram.current_command = 0;
							break;

						//Write byte
						case 0xA0: 
							flash_ram.write_single_byte = true;
							flash_ram.current_command = 0;
							break;

						//Bank switch
						case 0xB0:
							flash_ram.switch_bank = true;
							flash_ram.current_command = 0;
							break;

						//FLASH ID end
						case 0xF0: 
							flash_ram.grab_ids = false;
							flash_ram.current_command = 0; 
							break;

						default: std::cout<<"MMU::Unknown FLASH RAM command 0x" << std::hex << (int)value << "\n"; break;
					}
				}
			}

			break;

		case FLASH_RAM_CMD1:
			memory_map[address] = value;

			if(((current_save_type == FLASH_64) || (current_save_type == FLASH_128)) && (value == 0x55))
			{
				if(flash_ram.current_command == 1) { flash_ram.current_command++; }
			}
		
			break;

		case FLASH_RAM_SEC0:
		case FLASH_RAM_SEC1:
		case FLASH_RAM_SEC2:
		case FLASH_RAM_SEC3:
		case FLASH_RAM_SEC4:
		case FLASH_RAM_SEC5:
		case FLASH_RAM_SEC6:
		case FLASH_RAM_SEC7:
		case FLASH_RAM_SEC8:
		case FLASH_RAM_SEC9:
		case FLASH_RAM_SECA:
		case FLASH_RAM_SECB:
		case FLASH_RAM_SECC:
		case FLASH_RAM_SECD:
		case FLASH_RAM_SECE:
		case FLASH_RAM_SECF:
			memory_map[address] = value;

			if(((current_save_type == FLASH_64) || (current_save_type == FLASH_128)) && (value == 0x30) && (flash_ram.current_command == 2))
			{
				flash_erase_sector(address);
				flash_ram.current_command = 0;
			}
			
			else if((current_save_type == FLASH_128) && (address == FLASH_RAM_SEC0) && (flash_ram.switch_bank) && (flash_ram.current_command == 0))
			{
				flash_ram.bank = value;
				flash_ram.switch_bank = false;
			}
			
			break;


		default:
			memory_map[address] = value;
	}

	//Mirror memory from 0x3007FXX to 0x3FFFFXX
	if((address >= 0x3007F00) && (address <= 0x3007FFF)) 
	{
		u32 mirror_addr = 0x03FFFF00 + (address & 0xFF);
		memory_map[mirror_addr] = value;
	}

	//Trigger BG palette update in LCD
	else if((address >= 0x5000000) && (address <= 0x50001FF))
	{
		lcd_stat->bg_pal_update = true;
		lcd_stat->bg_pal_update_list[(address & 0x1FF) >> 1] = true;
	}

	//Trigger OBJ palette update in LCD
	else if((address >= 0x5000200) && (address <= 0x50003FF))
	{
		lcd_stat->obj_pal_update = true;
		lcd_stat->obj_pal_update_list[(address & 0x1FF) >> 1] = true;
	}

	//Trigger OAM update in LCD
	else if((address >= 0x7000000) && (address <= 0x70003FF) && (lcd_stat->oam_access))
	{
		lcd_stat->oam_update = true;
		lcd_stat->oam_update_list[(address & 0x3FF) >> 3] = true;
	}

	//Write to FLASH RAM
	else if(((current_save_type == FLASH_64) || (current_save_type == FLASH_128)) && (flash_ram.next_write) && (address >= 0xE000000) && (address <= 0xE00FFFF))
	{
			flash_ram.data[flash_ram.bank][(address & 0xFFFF)] = value;
			flash_ram.next_write = false;
	}

	if(flash_ram.write_single_byte) 
	{ 
		flash_ram.write_single_byte = false;
		flash_ram.next_write = true;
	}
}

/****** Write 2 bytes into memory ******/
void AGB_MMU::write_u16(u32 address, u16 value)
{
	write_u8(address, (value & 0xFF));
	write_u8((address+1), ((value >> 8) & 0xFF));
}

/****** Write 4 bytes into memory ******/
void AGB_MMU::write_u32(u32 address, u32 value)
{
	write_u8(address, (value & 0xFF));
	write_u8((address+1), ((value >> 8) & 0xFF));
	write_u8((address+2), ((value >> 16) & 0xFF));
	write_u8((address+3), ((value >> 24) & 0xFF));
}

/****** Writes 2 bytes into memory - No checks done on the read, used for known memory locations such as registers ******/
void AGB_MMU::write_u16_fast(u32 address, u16 value)
{
	memory_map[address] = (value & 0xFF);
	memory_map[address+1] = ((value >> 8) & 0xFF);
}

/****** Writes 4 bytes into memory - No checks done on the read, used for known memory locations such as registers ******/
void AGB_MMU::write_u32_fast(u32 address, u32 value)
{
	memory_map[address] = (value & 0xFF);
	memory_map[address+1] = ((value >> 8) & 0xFF);
	memory_map[address+2] = ((value >> 16) & 0xFF);
	memory_map[address+3] = ((value >> 24) & 0xFF);
}	

/****** Read binary file to memory ******/
bool AGB_MMU::read_file(std::string filename)
{
	std::ifstream file(filename.c_str(), std::ios::binary);

	if(!file.is_open()) 
	{
		std::cout<<"MMU::" << filename << " could not be opened. Check file path or permissions. \n";
		return false;
	}

	//Get the file size
	file.seekg(0, file.end);
	u32 file_size = file.tellg();
	file.seekg(0, file.beg);

	u8* ex_mem = &memory_map[0x8000000];

	//Read data from the ROM file
	file.read((char*)ex_mem, file_size);

	file.close();
	std::cout<<"MMU::" << filename << " loaded successfully. \n";

	//Calculate 8-bit checksum
	u8 checksum = 0;

	for(u32 x = 0x80000A0; x < 0x80000BD; x++) { checksum = checksum - memory_map[x]; }

	checksum = checksum - 0x19;

	if(checksum != memory_map[0x80000BD]) 
	{
		std::cout<<"MMU::Warning - Cartridge Header Checksum is 0x" << std::hex << (int)memory_map[0x80000BD] <<". Correct value is 0x" << (int)checksum << "\n";
	}

	std::string backup_file = filename + ".sav";

	//Try to auto-detect save-type, if any
	for(u32 x = 0x8000000; x < (0x8000000 + file_size); x+=4)
	{
		switch(memory_map[x])
		{
			//EEPROM
			case 0x45:
				if((memory_map[x+1] == 0x45) && (memory_map[x+2] == 0x50) && (memory_map[x+3] == 0x52) && (memory_map[x+4] == 0x4F) && (memory_map[x+5] == 0x4D))
				{
					std::cout<<"MMU::EEPROM save type detected\n";
					current_save_type = EEPROM;
					load_backup(backup_file);
					return true;
				}
				
				break;

			//FLASH RAM
			case 0x46:
				//64KB "FLASH_Vnnn"
				if((memory_map[x+1] == 0x4C) && (memory_map[x+2] == 0x41) && (memory_map[x+3] == 0x53) && (memory_map[x+4] == 0x48) && (memory_map[x+5] == 0x5F))
				{
					std::cout<<"MMU::FLASH RAM (64KB) save type detected\n";
					current_save_type = FLASH_64;
					load_backup(backup_file);
					return true;
				}

				//64KB "FLASH512_Vnnn"
				else if((memory_map[x+1] == 0x4C) && (memory_map[x+2] == 0x41) && (memory_map[x+3] == 0x53) && (memory_map[x+4] == 0x48) && (memory_map[x+5] == 0x35)
				&& (memory_map[x+6] == 0x31) && (memory_map[x+7] == 0x32)) 
				{
					std::cout<<"MMU::FLASH RAM (64KB) save type detected\n";
					current_save_type = FLASH_64;
					load_backup(backup_file);
					return true;
				}

				//128KB "FLASH1M_V"
				else if((memory_map[x+1] == 0x4C) && (memory_map[x+2] == 0x41) && (memory_map[x+3] == 0x53) && (memory_map[x+4] == 0x48) && (memory_map[x+5] == 0x31)
				&& (memory_map[x+6] == 0x4D))
				{
					std::cout<<"MMU::FLASH RAM (128KB) save type detected\n";
					current_save_type = FLASH_128;
					load_backup(backup_file);
					return true;
				}

				break;

			//SRAM
			case 0x53:
				if((memory_map[x+1] == 0x52) && (memory_map[x+2] == 0x41) && (memory_map[x+3] == 0x4D))
				{
					std::cout<<"MMU::SRAM save type detected\n";
					current_save_type = SRAM;
					load_backup(backup_file);
					return true;
				}

				break;
		}
	}
		
	return true;
}

/****** Read BIOS file into memory ******/
bool AGB_MMU::read_bios(std::string filename)
{
	std::ifstream file(filename.c_str(), std::ios::binary);

	if(!file.is_open()) 
	{
		std::cout<<"MMU::BIOS file " << filename << " could not be opened. Check file path or permissions. \n";
		return false;
	}

	//Get the file size
	file.seekg(0, file.end);
	u32 file_size = file.tellg();
	file.seekg(0, file.beg);

	u8* ex_mem = &memory_map[0];

	//Read data from the ROM file
	file.read((char*)ex_mem, file_size);

	file.close();
	std::cout<<"MMU::BIOS file " << filename << " loaded successfully. \n";

	return true;
}

/****** Load backup save data ******/
bool AGB_MMU::load_backup(std::string filename)
{
	std::ifstream file(filename.c_str(), std::ios::binary);
	std::vector<u8> save_data;

	if(!file.is_open()) 
	{
		std::cout<<"MMU::" << filename << " save data could not be opened. Check file path or permissions. \n";
		return false;
	}

	//Get the file size
	file.seekg(0, file.end);
	u32 file_size = file.tellg();
	file.seekg(0, file.beg);
	save_data.resize(file_size);

	//Load SRAM
	if(current_save_type == SRAM)
	{
		if(file_size > 0x8000) { std::cout<<"MMU::Warning - Irregular backup save size\n"; }

		//Read data from file
		file.read(reinterpret_cast<char*> (&save_data[0]), file_size);

		//Write that data into 0xE000000 to 0xE007FFF
		for(u32 x = 0; x <= 0x7FFF; x++)
		{
			memory_map[0xE000000 + x] = save_data[x];
			memory_map[0xE008000 + x] = 0xFF;
		}
	}

	//Load EEPROM
	else if(current_save_type == EEPROM)
	{
		if((file_size != 0x200) && (file_size != 0x2000)) { file_size = 0x200; std::cout<<"MMU::Warning - Irregular backup save size\n"; }

		//Read data from file
		file.read(reinterpret_cast<char*> (&save_data[0]), file_size);

		//Clear eeprom data and resize
		eeprom.size = file_size;
		eeprom.data.clear();
		eeprom.data.resize(file_size, 0);

		//Write that data into EEPROM
		for(u32 x = 0; x < file_size; x++)
		{
			eeprom.data[x] = save_data[x];
		}

		eeprom.size_lock = true;
	}

	//Load 64KB FLASH RAM
	else if(current_save_type == FLASH_64)
	{
		//Read data from file
		file.read(reinterpret_cast<char*> (&save_data[0]), file_size);

		//Write that data into 0xE000000 to 0xE00FFFF of FLASH RAM
		for(u32 x = 0; x < 0x10000; x++)
		{
			flash_ram.data[0][x] = save_data[x];
		}
	}

	//Load 128KB FLASH RAM
	else if(current_save_type == FLASH_128)
	{
		//Read data from file
		file.read(reinterpret_cast<char*> (&save_data[0]), file_size);

		//Write that data into 0xE000000 to 0xE00FFFF of FLASH RAM
		for(u32 x = 0; x < 0x10000; x++)
		{
			flash_ram.data[0][x] = save_data[x];
		}

		for(u32 x = 0x10000; x < 0x20000; x++)
		{
			flash_ram.data[1][x - 0x10000] = save_data[x];
		}
	}

	file.close();

	std::cout<<"MMU::Loaded save data file " << filename <<  "\n";

	return true;
}

/****** Save backup save data ******/
bool AGB_MMU::save_backup(std::string filename)
{
	//Save SRAM
	if(current_save_type == SRAM)
	{
		std::ofstream file(filename.c_str(), std::ios::binary);
		std::vector<u8> save_data;

		if(!file.is_open()) 
		{
			std::cout<<"MMU::" << filename << " save data could not be written. Check file path or permissions. \n";
			return false;
		}


		//Grab data from 0xE000000 to 0xE007FFF
		for(u32 x = 0; x <= 0x7FFF; x++)
		{
			save_data.push_back(memory_map[0xE000000 + x]);
		}

		//Write the data to a file
		file.write(reinterpret_cast<char*> (&save_data[0]), 0x7FFF);
		file.close();

		std::cout<<"MMU::Wrote save data file " << filename <<  "\n";
	}

	//Save EEPROM
	else if(current_save_type == EEPROM)
	{
		std::ofstream file(filename.c_str(), std::ios::binary);
		std::vector<u8> save_data;

		if(!file.is_open()) 
		{
			std::cout<<"MMU::" << filename << " save data could not be written. Check file path or permissions. \n";
			return false;
		}

		//Grab data from EEPROM
		for(u32 x = 0; x < eeprom.size; x++)
		{
			save_data.push_back(eeprom.data[x]);
		}

		//Write the data to a file
		file.write(reinterpret_cast<char*> (&save_data[0]), eeprom.size);
		file.close();

		std::cout<<"MMU::Wrote save data file " << filename <<  "\n";
	}

	//Save 64KB FLASH RAM
	else if(current_save_type == FLASH_64)
	{
		std::ofstream file(filename.c_str(), std::ios::binary);
		std::vector<u8> save_data;

		if(!file.is_open()) 
		{
			std::cout<<"MMU::" << filename << " save data could not be written. Check file path or permissions. \n";
			return false;
		}


		//Grab data from 0xE000000 to 0xE00FFFF from FLASH RAM
		for(u32 x = 0; x < 0x10000; x++)
		{
			save_data.push_back(flash_ram.data[0][x]);
		}

		//Write the data to a file
		file.write(reinterpret_cast<char*> (&save_data[0]), 0x10000);
		file.close();

		std::cout<<"MMU::Wrote save data file " << filename <<  "\n";
	}

	//Save 128KB FLASH RAM
	else if(current_save_type == FLASH_128)
	{
		std::ofstream file(filename.c_str(), std::ios::binary);
		std::vector<u8> save_data;

		if(!file.is_open()) 
		{
			std::cout<<"MMU::" << filename << " save data could not be written. Check file path or permissions. \n";
			return false;
		}


		//Grab data from 0xE000000 to 0xE00FFFF from FLASH RAM
		for(u32 x = 0; x < 0x10000; x++)
		{
			save_data.push_back(flash_ram.data[0][x]);
		}

		for(u32 x = 0; x < 0x10000; x++)
		{
			save_data.push_back(flash_ram.data[1][x]);
		}

		//Write the data to a file
		file.write(reinterpret_cast<char*> (&save_data[0]), 0x20000);
		file.close();

		std::cout<<"MMU::Wrote save data file " << filename <<  "\n";
	}

	return true;
}

/****** Start the DMA channels during blanking periods ******/
void AGB_MMU::start_blank_dma()
{
	//Repeat bits automatically enable DMAs
	if(dma[0].control & 0x200) { dma[0].enable = true; }
	if(dma[3].control & 0x200) { dma[3].enable = true; }

	//DMA0
	if(dma[0].enable)
	{
		u8 dma_type = ((dma[0].control >> 12) & 0x3);
		
		if(dma_type == 2) { dma[0].started = true; }
	}

	//DMA3
	if(dma[3].enable)
	{
		u8 dma_type = ((dma[3].control >> 12) & 0x3);
		
		if(dma_type == 2) { dma[3].started = true; }
	}
}

/****** Set EEPROM read-write address ******/
void AGB_MMU::eeprom_set_addr()
{
	//Clear EEPROM address
	eeprom.address = 0;

	//Skip 2 bits in the bitstream
	eeprom.dma_ptr += 4;

	//Read 6 or 14 bits from the bitstream, MSB 1st
	u16 bits = 0x20;
	u8 bit_length = 6;
	
	if(eeprom.size == 0x2000) { bits = 0x2000; bit_length = 14; }

	for(int x = 0; x < bit_length; x++)
	{
		u16 bitstream = read_u16(eeprom.dma_ptr);
		
		//If bit 0 of the halfword in the bitstream is set, flip the bit for the EEPROM address as well
		if(bitstream & 0x1) { eeprom.address |= bits; }

		eeprom.dma_ptr += 2;
		bits >>= 1;
	}
}

/****** Read EEPROM data ******/
void AGB_MMU::eeprom_read_data()
{
	//First 4 bits of the stream are ignored, send 0
	for(int x = 0; x < 4; x++) 
	{ 
		write_u16(eeprom.dma_ptr, 0x0);
		eeprom.dma_ptr += 2;
	}
	
	u16 temp_addr = (eeprom.address * 8);
	u8 bits = 0x80;

	//Get 64 bits from EEPROM, MSB 1st, write them to address pointed by the DMA (as halfwords)
	for(int x = 0; x < 64; x++)
	{
		u8 bitstream = (eeprom.data[temp_addr] & bits) ? 1 : 0;
		bits >>= 1;

		//Write stream to address provided by DMA
		write_u16(eeprom.dma_ptr, bitstream);
		eeprom.dma_ptr += 2;

		//On the 8th bit, move to next 8 bits in EEPROM, reload stuff
		if(bits == 0) 
		{
			temp_addr++;
			bits = 0x80;
		}
	}
}

/****** Write EEPROM data ******/
void AGB_MMU::eeprom_write_data()
{
	//Clear EEPROM address
	eeprom.address = 0;

	//Skip 2 bits in the bitstream
	eeprom.dma_ptr += 4;

	//Read 6 or 14 bits from the bitstream, MSB 1st
	u16 bits = 0x20;
	u8 bit_length = 6;
	
	if(eeprom.size == 0x2000) { bits = 0x2000; bit_length = 14; }

	for(int x = 0; x < bit_length; x++)
	{
		u16 bitstream = read_u16(eeprom.dma_ptr);
		
		//If bit 0 of the halfword in the bitstream is set, flip the bit for the EEPROM address as well
		if(bitstream & 0x1) { eeprom.address |= bits; }

		eeprom.dma_ptr += 2;
		bits >>= 1;
	}

	u8 temp_byte = 0;
	u16 temp_addr = (eeprom.address * 8);
	bits = 0x80;

	//Read 64 bits from the bitstream to store at EEPROM address, MSB 1st
	for(int x = 0; x < 64; x++)
	{
		u16 bitstream = read_u16(eeprom.dma_ptr);

		if(bitstream & 0x1) { temp_byte |= bits; }

		eeprom.dma_ptr += 2;
		bits >>= 1;

		//On the 8th bit, send the data to EEPROM, reload stuff
		if(bits == 0) 
		{
			eeprom.data[temp_addr] = temp_byte;
			temp_byte = 0;
			temp_addr++;
			bits = 0x80;
		}
	}

	memory_map[0xD000000] = 0x1;
}

/****** Erase entire FLASH RAM ******/
void AGB_MMU::flash_erase_chip()
{
	for(int x = 0; x < 0x10000; x++) 
	{ 
		flash_ram.data[0][x] = 0xFF;
		flash_ram.data[1][x] = 0xFF; 
	}
}

/****** Erase 4KB sector of FLASH RAM ******/
void AGB_MMU::flash_erase_sector(u32 sector)
{
	for(u32 x = sector; x < (sector + 0x1000); x++) 
	{ 
		flash_ram.data[flash_ram.bank][(x & 0xFFFF)] = 0xFF; 
	}
}	

/****** Points the MMU to an lcd_data structure (FROM THE LCD ITSELF) ******/
void AGB_MMU::set_lcd_data(agb_lcd_data* ex_lcd_stat) { lcd_stat = ex_lcd_stat; }

/****** Points the MMU to an apu_data structure (FROM THE APU ITSELF) ******/
void AGB_MMU::set_apu_data(agb_apu_data* ex_apu_stat) { apu_stat = ex_apu_stat; }
