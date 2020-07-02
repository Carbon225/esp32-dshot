#include "DShotRMT.h"
#include "freertos/task.h"

#define LOG_LOCAL_LEVEL ESP_LOG_DEBUG
#include "esp_log.h"

static const char *TAG = "dshot-rmt";

#define DSHOT_ERROR_CHECK(x) ({        \
	esp_err_t __ret = x;               \
	if (__ret != ESP_OK)               \
		return __ret;                  \
	__ret;                             \
})

// from https://github.com/bitdump/BLHeli/blob/master/BLHeli_32%20ARM/BLHeli_32%20Firmware%20specs/Digital_Cmd_Spec.txt
enum dshot_cmd_t
{
	DIGITAL_CMD_MOTOR_STOP, 		      // Currently not implemented
	DIGITAL_CMD_BEEP1, 			      // Wait at least length of beep (260ms) before next command
	DIGITAL_CMD_BEEP2, 			      // Wait at least length of beep (260ms) before next command
	DIGITAL_CMD_BEEP3, 			      // Wait at least length of beep (280ms) before next command
	DIGITAL_CMD_BEEP4, 			      // Wait at least length of beep (280ms) before next command
	DIGITAL_CMD_BEEP5, 			      // Wait at least length of beep (1020ms) before next command
	DIGITAL_CMD_ESC_INFO,  		      // Wait at least 12ms before next command
	DIGITAL_CMD_SPIN_DIRECTION_1, 	      // Need 6x, no wait required
	DIGITAL_CMD_SPIN_DIRECTION_2, 	      // Need 6x, no wait required
	DIGITAL_CMD_3D_MODE_OFF, 		      // Need 6x, no wait required
	DIGITAL_CMD_3D_MODE_ON,  		      // Need 6x, no wait required
	DIGITAL_CMD_SETTINGS_REQUEST,  	      // Currently not implemented
	DIGITAL_CMD_SAVE_SETTINGS,  		      // Need 6x, wait at least 35ms before next command
	DIGITAL_CMD_SPIN_DIRECTION_NORMAL = 20, 	      // Need 6x, no wait required
	DIGITAL_CMD_SPIN_DIRECTION_REVERSED, 	      // Need 6x, no wait required
	DIGITAL_CMD_LED0_ON, 			      // No wait required
	DIGITAL_CMD_LED1_ON, 			      // No wait required
	DIGITAL_CMD_LED2_ON, 			      // No wait required
	DIGITAL_CMD_LED3_ON, 			      // No wait required
	DIGITAL_CMD_LED0_OFF, 		      // No wait required
	DIGITAL_CMD_LED1_OFF, 		      // No wait required
	DIGITAL_CMD_LED2_OFF, 		      // No wait required
	DIGITAL_CMD_LED3_OFF, 		      // No wait required
};

// DSHOT Timings
#define DSHOT_TICKS_PER_BIT 19

#define DSHOT_T0H 7
#define DSHOT_T0L (DSHOT_TICKS_PER_BIT - DSHOT_T0H)

#define DSHOT_T1H 14
#define DSHOT_T1L (DSHOT_TICKS_PER_BIT - DSHOT_T1H)

#define DSHOT_PAUSE (DSHOT_TICKS_PER_BIT * 200)
// !DSHOT Timings

#define RMT_CMD_SIZE (sizeof(_dshotCmd) / sizeof(_dshotCmd[0]))

#define DSHOT_THROTTLE_MIN 48
#define DSHOT_THROTTLE_MAX 2047

#define DSHOT_ARM_DELAY (5000 / portTICK_PERIOD_MS)

DShotRMT::DShotRMT()
{
	// initialize cmd buffer
	setData(0);

	// DShot packet delay + RMT end marker
	_dshotCmd[16].duration0 = DSHOT_PAUSE;
	_dshotCmd[16].level0 = 0;
	_dshotCmd[16].duration1 = 0;
	_dshotCmd[16].level1 = 0;
}

DShotRMT::~DShotRMT()
{
	// TODO write destructor
}

esp_err_t DShotRMT::install(gpio_num_t gpio, rmt_channel_t rmtChannel)
{
	_rmtChannel = rmtChannel;

	rmt_config_t config;

	config.channel = rmtChannel;
	config.rmt_mode = RMT_MODE_TX;
	config.gpio_num = gpio;
	config.mem_block_num = 1;
	config.clk_div = 7;

	config.tx_config.loop_en = false;
	config.tx_config.carrier_en = false;
	config.tx_config.idle_level = RMT_IDLE_LEVEL_LOW;
	config.tx_config.idle_output_en = true;

	DSHOT_ERROR_CHECK(rmt_config(&config));

	return rmt_driver_install(rmtChannel, 0, 0);
}

esp_err_t DShotRMT::uninstall()
{
	// TODO implement uninstall
	return ESP_OK;
}

esp_err_t DShotRMT::init(bool wait)
{
	ESP_LOGD(TAG, "Sending reset command");
	for (int i = 0; i < 50; i++)
	{
		writeData(0, true);
	}

	ESP_LOGD(TAG, "Sending idle throttle");
	if (wait)
		DSHOT_ERROR_CHECK(repeatPacketTicks({DSHOT_THROTTLE_MIN, 0}, DSHOT_ARM_DELAY));
	else
		writePacket({DSHOT_THROTTLE_MIN, 0}, false);

	ESP_LOGD(TAG, "ESC armed");
	return ESP_OK;
}

esp_err_t DShotRMT::sendThrottle(uint16_t throttle)
{
	if (throttle < DSHOT_THROTTLE_MIN || throttle > DSHOT_THROTTLE_MAX)
		return ESP_ERR_INVALID_ARG;

	return writePacket({throttle, 0}, false);
}

esp_err_t DShotRMT::setReversed(bool reversed)
{
	DSHOT_ERROR_CHECK(rmt_wait_tx_done(_rmtChannel, 1));
	DSHOT_ERROR_CHECK(repeatPacketTicks({DSHOT_THROTTLE_MIN, 0}, 200 / portTICK_PERIOD_MS));
	DSHOT_ERROR_CHECK(repeatPacket(
			{reversed ? DIGITAL_CMD_SPIN_DIRECTION_REVERSED : DIGITAL_CMD_SPIN_DIRECTION_NORMAL, 1},
			10));
	return ESP_OK;
}

esp_err_t DShotRMT::beep()
{
	DSHOT_ERROR_CHECK(writePacket({DIGITAL_CMD_BEEP1, 1}, true));
	vTaskDelay(260 / portTICK_PERIOD_MS);
	return ESP_OK;
}

void DShotRMT::setData(uint16_t data)
{
	for (int i = 0; i < 16; i++, data <<= 1)
	{
		if (data & 0x8000)
		{
			// set one
			_dshotCmd[i].duration0 = DSHOT_T1H;
			_dshotCmd[i].level0 = 1;
			_dshotCmd[i].duration1 = DSHOT_T1L;
			_dshotCmd[i].level1 = 0;
		}
		else
		{
			// set zero
			_dshotCmd[i].duration0 = DSHOT_T0H;
			_dshotCmd[i].level0 = 1;
			_dshotCmd[i].duration1 = DSHOT_T0L;
			_dshotCmd[i].level1 = 0;
		}
	}
}

uint8_t DShotRMT::checksum(uint16_t data)
{
	uint16_t csum = 0;

	for (int i = 0; i < 3; i++)
	{
		csum ^= data;
		data >>= 4;
	}

	return csum & 0xf;
}

esp_err_t DShotRMT::writeData(uint16_t data, bool wait)
{
	DSHOT_ERROR_CHECK(rmt_wait_tx_done(_rmtChannel, 0));

	setData(data);

	return rmt_write_items(_rmtChannel,
						   _dshotCmd, RMT_CMD_SIZE,
						   wait);
}

esp_err_t DShotRMT::writePacket(dshot_packet_t packet, bool wait)
{
	uint16_t data = packet.payload;

	data <<= 1;
	data |= packet.telemetry;

	data = (data << 4) | checksum(data);

	return writeData(data, wait);
}

esp_err_t DShotRMT::repeatPacket(dshot_packet_t packet, int n)
{
	for (int i = 0; i < n; i++)
	{
		DSHOT_ERROR_CHECK(writePacket(packet, true));
		portYIELD();
	}
	return ESP_OK;
}

esp_err_t DShotRMT::repeatPacketTicks(dshot_packet_t packet, TickType_t ticks)
{
	DSHOT_ERROR_CHECK(rmt_wait_tx_done(_rmtChannel, ticks));

	TickType_t repeatStop = xTaskGetTickCount() + ticks;
	while (xTaskGetTickCount() < repeatStop)
	{
		DSHOT_ERROR_CHECK(writePacket(packet, false));
		vTaskDelay(1);
	}
	return ESP_OK;
}
