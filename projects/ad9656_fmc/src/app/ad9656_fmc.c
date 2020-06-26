/***************************************************************************//**
 *   @file   ad9656_fmc.c
 *   @brief  Implementation of Main Function.
 *   @author DHotolea (dan.hotoleanu@analog.com)
 *******************************************************************************
 * Copyright 2020(c) Analog Devices, Inc.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *  - Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  - Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  - Neither the name of Analog Devices, Inc. nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *  - The use of this software may or may not infringe the patent rights
 *    of one or more patent holders.  This license does not release you
 *    from the requirement that you obtain separate licenses from these
 *    patent holders to use this software.
 *  - Use of the software either in source or binary form, must be run
 *    on or directly connected to an Analog Devices Inc. component.
 *
 * THIS SOFTWARE IS PROVIDED BY ANALOG DEVICES "AS IS" AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, NON-INFRINGEMENT,
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL ANALOG DEVICES BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, INTELLECTUAL PROPERTY RIGHTS, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ******************************************************************************/

/******************************************************************************/
/***************************** Include Files **********************************/
/******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include "ad9656.h"
#include "axi_adc_core.h"
#include "axi_dmac.h"
#include "axi_adxcvr.h"
#include "axi_jesd204_rx.h"
#include "spi_extra.h"
#include "parameters.h"
#include "error.h"
#include "delay.h"

/******************************************************************************/
/********************** Macros and Constants Definitions **********************/
/******************************************************************************/

#define DMA_BUFFER		 0

struct ad9508_init_param {
	/* SPI */
	struct spi_init_param	spi_init;
};

struct ad9553_init_param {
	/* SPI */
	struct spi_init_param	spi_init;
};

struct ad9508_dev {
	/* SPI */
	struct spi_desc	*spi_desc;
};

struct ad9553_dev {
	/* SPI */
	struct spi_desc	*spi_desc;
};

/**
 * @brief Reads from the ad9508 that is contected to the SPI
 * @param dev - The ad9508 device handler
 * @param reg_addr - The address of the internal register of the ad9508 chip
 * @param reg_data - The value read from the internal register
 * @return SUCCESS if the value was successfully read, FAILURE otherwise
 */
int32_t ad9508_spi_read(struct ad9508_dev *dev,
			uint16_t reg_addr,
			uint8_t *reg_data)
{
	uint8_t buf[3];

	int32_t ret;

	// the MSB of byte 0 indicates a r/w operation, following 7 bits are the
	// bits 14-8 of the address of the register that is accessed. Byte 1
	// contains the bits 7-0 of the address of the register.
	buf[0] = 0x80 | (reg_addr >> 8);
	buf[1] = reg_addr & 0xFF;
	buf[2] = 0x00;

	ret = spi_write_and_read(dev->spi_desc, buf, 3);

	if (ret < 0)
		return ret;

	*reg_data = buf[2];

	return ret;
}

/**
 * @brief Reads from the ad9553 that is contected to the SPI
 * @param dev - The ad9553 device handler
 * @param reg_addr - The address of the internal register of the ad9553 chip
 * @param reg_data - The value read from the internal register
 * @return SUCCESS if the value was successfully read, FAILURE otherwise
 */
int32_t ad9553_spi_read(struct ad9553_dev *dev,
			uint16_t reg_addr,
			uint8_t *reg_data)
{
	uint8_t buf[3];

	int32_t ret;

	// the MSB of byte 0 indicates a r/w operation, following 7 bits are the 
	// bits 14-8 of the address of the register that is accessed. Byte 1 
	// contains the bits 7-0 of the address of the register.
	buf[0] = 0x80 | (reg_addr >> 8);
	buf[1] = reg_addr & 0xFF;
	buf[2] = 0x00;

	ret = spi_write_and_read(dev->spi_desc, buf, 3);
	
	if (ret < 0)
		return ret;

	*reg_data = buf[2];

	return ret;
}

/**
 * @brief Write to the ad9508 that is conected to the SPI
 * @param dev - The device handler for the ad9508 chip
 * @param reg_addr - Address of the internal register of the ad9508 chip
 * @param reg_data - Value to be written to the register
 * @return SUCCESS if the value was written successfully, FAILURE otherwise
 */
int32_t ad9508_spi_write(struct ad9508_dev *dev,
			 uint16_t reg_addr,
			 uint8_t reg_data)
{
	uint8_t buf[3];

	// the MSB of byte 0 indicates a r/w operation, following 7 bits are the
	// bits 14-8 of the address of the register that is accessed. Byte 1
	// contains the bits 7-0 of the address of the register. Byte 2 contains
	// the data to be written.
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	buf[2] = reg_data;

	return spi_write_and_read(dev->spi_desc, buf, 3);
}

/**
 * @brief Write to the ad9553 that is conected to the SPI
 * @param dev - The device handler for the ad9553 chip
 * @param reg_addr - Address of the internal register of the ad9553 chip
 * @param reg_data - Value to be written to the register
 * @return SUCCESS if the value was written successfully, FAILURE otherwise
 */
int32_t ad9553_spi_write(struct ad9553_dev *dev,
			 uint16_t reg_addr,
			 uint8_t reg_data)
{
	uint8_t buf[3];

	// the MSB of byte 0 indicates a r/w operation, following 7 bits are the 
	// bits 14-8 of the address of the register that is accessed. Byte 1 
	// contains the bits 7-0 of the address of the register. Byte 2 contains
	// the data to be written.
	buf[0] = reg_addr >> 8;
	buf[1] = reg_addr & 0xFF;
	buf[2] = reg_data;

	return spi_write_and_read(dev->spi_desc, buf, 3);
}

/**
 * @brief Setup the working parameters of the ad9508 chip
 * @param device - The device handler of the ad9508 chip
 * @param init_param - Values for the working parameters of ad9508
 * @return SUCCESS if device is ready for use, FAILURE otherwise
 */
int ad9508_setup(struct ad9508_dev **device,
	     const struct ad9508_init_param *init_param)
{
	int32_t ret;
	
	struct ad9508_dev *dev;	
	uint8_t reg_data;

	dev = (struct ad9508_dev *)malloc(sizeof(*dev));
	if (!dev)
		return FAILURE;
	
	/* SPI */
	ret = spi_init(&dev->spi_desc, &init_param->spi_init);
	if (ret != SUCCESS)
		return ret;

	// reset 
	ad9508_spi_write(dev, 0x00, 0x24);

	// read family part id: 0x0C contains the least significant byte,
	//                      0x0D contains the most significant byte
	ad9508_spi_read(dev, 0x0C, &reg_data);
	if (reg_data != 0x05)
		return FAILURE;
	ad9508_spi_read(dev, 0x0D, &reg_data);
	if (reg_data != 0x00)
		return FAILURE;
	
	// configure 9508 to pass the 125MHz input clock unmodified, so divider = 1
	// no phase offset
	ad9508_spi_write(dev, 0x1B, 0x00); // divide ratio[7:0]
	ad9508_spi_write(dev, 0x1C, 0x00); // divide ratio[9:8]
	ad9508_spi_write(dev, 0x1D, 0x00); // phase offset[7:0]
	ad9508_spi_write(dev, 0x1E, 0x00); // phase offset[10:8]

	return SUCCESS;
}

/**
 * @brief Setup the working parameters of the ad9553 chip
 * @param device - The device handler of the ad9553 chip
 * @param init_param - Values for the working parameters of ad9553
 * @return SUCCESS if device is ready for use, FAILURE otherwise
 */
int ad9553_setup(struct ad9553_dev **device,
	     const struct ad9553_init_param *init_param)
{
	int32_t ret;
	
	struct ad9553_dev *dev;	

	dev = (struct ad9553_dev *)malloc(sizeof(*dev));
	if (!dev)
		return FAILURE;
	
	/* SPI */
	ret = spi_init(&dev->spi_desc, &init_param->spi_init);
	if (ret != SUCCESS)
		return ret;

	// reset 
	ad9553_spi_write(dev, 0x00, 0x3C);

	// enable SPI control of charge pump
	ad9553_spi_write(dev, 0x0B, 0xB0);
	
	// lock detector activated
	ad9553_spi_write(dev, 0x0D, 0x00);

	// P1 = 4
	// P1[9:2]
	ad9553_spi_write(dev, 0x15, 0x01);

	// P1[1:0], P2[9:4]
	ad9553_spi_write(dev, 0x16, 0x00);

	// P2[3:0]
	ad9553_spi_write(dev, 0x17, 0x00);

	// P0 = 7
	ad9553_spi_write(dev, 0x18, 0x60);

	// N = 700
	// N[19:12]
	ad9553_spi_write(dev, 0x12, 0x00);

	// N[11:4]
	ad9553_spi_write(dev, 0x13, 0x2B);

	// N[3:0], take the N value from the feedback divide register,
	// take the output divider values from the registers, reset 
	// counters and logic of the PLL
	ad9553_spi_write(dev, 0x14, 0xCD);

	mdelay(250);

	// RefA = 10
	// RefA[13:6] divider
	ad9553_spi_write(dev, 0x1F, 0x00);
	
	// RefA[5:0], use the value stored in RefA register for RefA divider value
	ad9553_spi_write(dev, 0x20, 0x2A);

	// k = 2/5
	// enable SPI control for x2 for RefA, select x2 for RefA, 
	// enable SPI control for :5 for RefA, select :5 for RefA
	ad9553_spi_write(dev, 0x21, 0xF0);

	// RefA is configured as a differential input so RefDiff = 1
	ad9553_spi_write(dev, 0x29, 0xA0);

	// Out1 drive strength diven by SPI configuration, the rest
	// of the settings for this register remain default
	ad9553_spi_write(dev, 0x32, 0xA9);

	// Out2 powered down
	ad9553_spi_write(dev, 0x34, 0xE8);

	return SUCCESS;
}

/**
 * @brief Free the resources allocated by ad9508_setup().
 * @param dev - The device structure.
 * @return SUCCESS in case of success, negative error code otherwise.
 */
int32_t ad9508_remove(struct ad9508_dev *dev)
{
	int32_t ret;

	ret = spi_remove(dev->spi_desc);

	free(dev);

	return ret;
}

/**
 * @brief Free the resources allocated by ad9553_setup().
 * @param dev - The device structure.
 * @return SUCCESS in case of success, negative error code otherwise.
 */
int32_t ad9553_remove(struct ad9553_dev *dev)
{
	int32_t ret;

	ret = spi_remove(dev->spi_desc);

	free(dev);

	return ret;
}

/***************************************************************************//**
 * @brief main
 ******************************************************************************/
int main(void)
{
	int32_t status;

	/* Initialize SPI structures */
	struct spi_init_param ad9508_spi_param = {
			.max_speed_hz = 2000000u,
			.chip_select = 1,
			.mode = SPI_MODE_0
	};

	struct spi_init_param ad9553_spi_param = {
			.max_speed_hz = 2000000u,
			.chip_select = 2,
			.mode = SPI_MODE_0
	};
	struct spi_init_param ad9656_spi_param = {
			.max_speed_hz = 2000000u,
			.chip_select = 0,
			.mode = SPI_MODE_0
	};

	struct xil_spi_init_param xil_spi_param = {
			.type = SPI_PS,
			.device_id = SPI_DEVICE_ID
	};

	ad9508_spi_param.platform_ops = &xil_platform_ops;
	ad9508_spi_param.extra = &xil_spi_param;
	ad9553_spi_param.platform_ops = &xil_platform_ops;
	ad9553_spi_param.extra = &xil_spi_param;
	ad9656_spi_param.platform_ops = &xil_platform_ops;
	ad9656_spi_param.extra = &xil_spi_param;

	struct ad9508_init_param	ad9508_param;
	struct ad9553_init_param	ad9553_param;
	struct ad9656_init_param	ad9656_param;

	ad9508_param.spi_init = ad9508_spi_param;
	ad9553_param.spi_init = ad9553_spi_param;
	ad9656_param.spi_init = ad9656_spi_param;

	struct ad9508_dev *ad9508_device;
	struct ad9553_dev *ad9553_device;
	struct ad9656_dev *ad9656_device;

//******************************************************************************
// setup the base addresses
//******************************************************************************

	struct adxcvr_init ad9656_xcvr_param = {
			.name = "ad9656_xcvr",
			.base = XPAR_AXI_AD9656_RX_XCVR_BASEADDR,
			.sys_clk_sel = 0,
			.out_clk_sel = 4,
			.lpm_enable = 1,
			.cpll_enable = 1,
			.ref_rate_khz = 125000,
			.lane_rate_khz = 2500000
		};

//******************************************************************************
// ADC (AD9656) and the receive path ( AXI_ADXCVR,
//	JESD204, AXI_AD9656, RX DMAC) configuration
//******************************************************************************

	/* JESD initialization */
	struct jesd204_rx_init  ad9656_jesd_param = {
			.name = "ad9656_jesd",
			.base = RX_JESD_BASEADDR,
			.octets_per_frame = 2,
			.frames_per_multiframe = 32,
			.subclass = 1,
			.device_clk_khz = 2500000/40,
			.lane_clk_khz = 2500000
	};

	struct axi_jesd204_rx *ad9656_jesd;

	/* ADC Core */
	struct axi_adc_init ad9656_core_param = {
			.name = "ad9656_adc",
			.base = RX_CORE_BASEADDR,
			.num_channels = 4
	};
	struct axi_adc	*ad9656_core;
	struct adxcvr	*ad9656_xcvr;

//******************************************************************************
// configure the receiver DMA
//******************************************************************************

	struct axi_dmac_init ad9656_dmac_param = {
			.name = "ad9656_dmac",
			.base = RX_DMA_BASEADDR,
			.direction = DMA_DEV_TO_MEM,
			.flags = 0
	};
	struct axi_dmac *ad9656_dmac;
	
//******************************************************************************
// bring up the system
//******************************************************************************

	ad9656_param.lane_rate_kbps = 2500000;

	// setup clocks
	if (ad9508_setup(&ad9508_device, &ad9508_param) != SUCCESS)
		printf("The ad9508 chip could not be setup correctly!\n");

	if (ad9553_setup(&ad9553_device, &ad9553_param) != SUCCESS)
		printf("The ad9553 chip could not be setup!\n");

	// ADC
	ad9656_setup(&ad9656_device, &ad9656_param);

	if (adxcvr_init(&ad9656_xcvr, &ad9656_xcvr_param) != SUCCESS) {
			printf("error: %s: adxcvr_init() failed\n", ad9656_xcvr->name);
	}

	if (adxcvr_clk_enable(ad9656_xcvr) != SUCCESS) {
			printf("error: %s: adxcvr_clk_enable() failed\n", ad9656_xcvr->name);
	}

	if (axi_jesd204_rx_init(&ad9656_jesd, &ad9656_jesd_param) != SUCCESS) {
		printf("error: %s: axi_jesd204_rx_init() failed\n", ad9656_jesd->name);
	}

	if (axi_jesd204_rx_lane_clk_enable(ad9656_jesd) != SUCCESS) {
		printf("error: %s: axi_jesd204_tx_lane_clk_enable() failed\n", ad9656_jesd->name);
	}

	status = axi_jesd204_rx_status_read(ad9656_jesd);
	if (status != SUCCESS) {
		printf("axi_jesd204_rx_status_read() error: %d\n", status);
	}

	if (axi_adc_init(&ad9656_core,  &ad9656_core_param) != SUCCESS) {
		printf("axi_adc_init() error: %s\n", ad9656_core->name);
	}

//******************************************************************************
// receive path testing
//******************************************************************************

	/* receive path testing */
	ad9656_test(ad9656_device, AD9656_TEST_PN9);
	if(axi_adc_pn_mon(ad9656_core, AXI_ADC_PN9, 10) == -1) {
		printf("%s ad9656 - PN9 sequence mismatch!\n", __func__);
	};
	ad9656_test(ad9656_device, AD9656_TEST_PN23);
	if(axi_adc_pn_mon(ad9656_core, AXI_ADC_PN23A, 10) == -1) {
		printf("%s ad9656 - PN23 sequence mismatch!\n", __func__);
	};

	ad9656_test(ad9656_device, AD9656_TEST_OFF);

//******************************************************************************
// external loopback - capture data with DMA
//******************************************************************************

	/* Initialize the DMAC and transfer 16384 samples from ADC to MEM */
	axi_dmac_init(&ad9656_dmac, &ad9656_dmac_param);
	axi_dmac_transfer(ad9656_dmac, ADC_DDR_BASEADDR, 16384 * 2);

	printf("ad9656: setup and configuration is done\n");

	/* Memory deallocation for devices and spi */
	ad9508_remove(ad9508_device);
	ad9553_remove(ad9553_device);
	ad9656_remove(ad9656_device);

	return(0);
}
