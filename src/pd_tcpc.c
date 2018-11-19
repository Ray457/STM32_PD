#include "platform.h"
#include "pd_phy.h"
#include "tcpci.h"

// internal_flags
#define TCPC_FLAG_RDRP_CHANGED (1<<0)

#define RX_BUFFER_SIZE 1

static struct pd_port_controller {
	/* current port power role (SOURCE or SINK) */
	uint8_t power_role;
	/* current port data role (DFP or UFP) */
	uint8_t data_role;

	/* Port polarity : 0 => CC1 is CC line, 1 => CC2 is CC line */
	uint8_t polarity;
	/* Our CC pull resistor setting */
	uint8_t cc_role_ctrl;
	/* CC status */
	uint8_t cc_status[2];

	/* TCPC alert status */
	uint16_t alert;
	uint16_t alert_mask;
	/* RX enabled */
	uint8_t rx_enabled;
	/* Power status */
	uint8_t power_status;
	uint8_t power_status_mask;

	/* Last received */
	int rx_head[RX_BUFFER_SIZE+1];
	uint32_t rx_payload[RX_BUFFER_SIZE+1][7];
	int rx_buf_head, rx_buf_tail;

	/* Next transmit */
	enum tcpm_transmit_type tx_type;
	uint16_t tx_head;
	uint32_t tx_payload[7];
	const uint32_t *tx_data;

	/* Internal Flags */
	uint32_t internal_flags;
	uint64_t cc_last_sampled_timestamp;
} pd;

static void alert(uint16_t mask)
{
	/* Always update the Alert status register */
	pd.alert |= mask;
	/*
	 * Only send interrupt to TCPM if corresponding
	 * bit in the alert_enable register is set.
	 */
	//if (pd.alert_mask & mask)
		//tcpc_alert(port);
}

static void tcpc_alert_status_clear(uint16_t mask)
{
	/*
	 * If the RX status alert is attempting to be cleared, then increment
	 * rx buffer tail pointer. if the RX buffer is not empty, then keep
	 * the RX status alert active.
	 */
//	if (mask & TCPC_REG_ALERT_RX_STATUS) {
//		if (!rx_buf_is_empty(port)) {
//			rx_buf_increment(port, &pd[port].rx_buf_tail);
//			if (!rx_buf_is_empty(port))
//				/* buffer is not empty, keep alert active */
//				mask &= ~TCPC_REG_ALERT_RX_STATUS;
//		}
//	}

	/* clear only the bits specified by the TCPM */
	pd.alert &= ~mask;

	/* Set Alert# inactive if all alert bits clear */
	//if (!pd[port].alert)
		//tcpc_alert_clear(port);
}

void tcpc_init(void)
{
	pd.power_status |= TCPC_REG_POWER_STATUS_UNINIT | TCPC_REG_POWER_STATUS_VBUS_DET;

	// Initialize CC pull-up/pull-down and Vconn controller
	pd_cc_rprp_init();

	// Initialize PD BMC transceiver
	pd_init();

	pd.cc_role_ctrl = TCPC_REG_ROLE_CTRL_SET(0, TYPEC_RP_USB, TYPEC_CC_RD, TYPEC_CC_RD);

	/* set default alert and power mask register values */
	pd.alert_mask = TCPC_REG_ALERT_MASK_ALL;
	pd.power_status_mask = TCPC_REG_POWER_STATUS_MASK_ALL;

	// Init done!
	pd.power_status &=~ TCPC_REG_POWER_STATUS_UNINIT;
	alert(TCPC_REG_ALERT_POWER_STATUS);
}

static void tcpc_i2c_write(uint32_t len, uint8_t *payload)
{
	uint16_t alert;

	/* If we are not yet initialized, ignore any write command */
	if (pd.power_status & TCPC_REG_POWER_STATUS_UNINIT)
		return;

	switch (payload[0]) {

	case TCPC_REG_ALERT:
		alert = payload[1];
		alert |= (payload[2] << 8);
		/* clear alert bits specified by the TCPM */
		tcpc_alert_status_clear(alert);
		break;
	case TCPC_REG_ALERT_MASK:
		alert = payload[1];
		alert |= (payload[2] << 8);
		//tcpc_alert_mask_set(alert);
		break;



	case TCPC_REG_ROLE_CTRL:
		/*
		 * Assume B3:2 and B1:0 are the same:
		 * Both open for NC
		 * Both Rd for source
		 * Both Rp for sink
		 *
		 * Cable requires to present a single Ra, this is done through connecting a physical 1k Resistor
		 */

		// Update DRP bit
		pd.cc_role_ctrl &=~ TCPC_REG_ROLE_CTRL_DRP_MASK;
		pd.cc_role_ctrl |= payload[1] & TCPC_REG_ROLE_CTRL_DRP_MASK;

		/* If CC pull resistor not changing, then nothing to do */
		if (TCPC_REG_ROLE_CTRL_CCXRP(pd.cc_role_ctrl) == TCPC_REG_ROLE_CTRL_CCXRP(payload[1]))
			return;

		/* Change CC pull resistor */
		pd.cc_role_ctrl = payload[1];	// DRP bit has been updated

		pd_cc_set(TCPC_REG_ROLE_CTRL_CCXRP(pd.cc_role_ctrl));

		// Set Rp and Rd
		pd_cc_set(payload[1]);

		/*
		 * Before CC pull can be changed and the task can read the new
		 * status, we should set the CC status to open, in case TCPM
		 * asks before it is known for sure.
		 */
		pd.cc_status[0] = TYPEC_CC_VOLT_OPEN;
		pd.cc_status[1] = TYPEC_CC_VOLT_OPEN;

		pd.cc_last_sampled_timestamp = timestamp_get();	// Postponed the next sampling of CC line, debouncing
		// Fire CC line change event ?
		/* Wake the PD phy task with special CC event mask */
		/* TODO: use top case if no TCPM on same CPU */
		//tcpc_run(port, PD_EVENT_CC);
		//task_set_event(PD_PORT_TO_TASK_ID(port), PD_EVENT_CC, 0);
		break;
	case TCPC_REG_POWER_CTRL:
		//tcpc_set_vconn(TCPC_REG_POWER_CTRL_VCONN(payload[1]));
		break;
	case TCPC_REG_TCPC_CTRL:
		//tcpc_set_polarity(TCPC_REG_TCPC_CTRL_POLARITY(payload[1]));
		break;
	case TCPC_REG_MSG_HDR_INFO:
		//tcpc_set_msg_header(TCPC_REG_MSG_HDR_INFO_PROLE(payload[1]),
		//		    TCPC_REG_MSG_HDR_INFO_DROLE(payload[1]));
		break;

	case TCPC_REG_RX_DETECT:
		//tcpc_set_rx_enable(payload[1] & TCPC_REG_RX_DETECT_SOP_HRST_MASK);
		break;
	case TCPC_REG_POWER_STATUS_MASK:
		//tcpc_set_power_status_mask(payload[1]);
		break;
	case TCPC_REG_TX_HDR:
		pd.tx_head = (payload[2] << 8) | payload[1];
		break;
	case TCPC_REG_TX_DATA:
		//memcpy(pd.tx_payload, &payload[1], len - 1);
		break;
	case TCPC_REG_TRANSMIT:
		//tcpc_transmit(TCPC_REG_TRANSMIT_TYPE(payload[1]),
		//	      pd.tx_head, pd.tx_payload);
		break;
	}
}

static int tcpc_i2c_read(uint8_t *payload)
{
	int cc1, cc2;
	int alert;

	switch (payload[0]) {

	case TCPC_REG_ALERT:
		payload[0] = pd.alert & 0xff;
		payload[1] = (pd.alert >> 8) & 0xff;
		return 2;
	case TCPC_REG_ALERT_MASK:
		payload[0] = pd.alert_mask & 0xff;
		payload[1] = (pd.alert_mask >> 8) & 0xff;
		return 2;
	case TCPC_REG_POWER_STATUS_MASK:
		payload[0] = pd.power_status_mask;
		return 1;

	case TCPC_REG_CC_STATUS:
		payload[0] = TCPC_REG_CC_STATUS_SET(
				TCPC_REG_ROLE_CTRL_CC2(pd.cc_role_ctrl) == TYPEC_CC_RD,
				pd.cc_status[0], pd.cc_status[1]);
		return 1;
	case TCPC_REG_ROLE_CTRL:
		payload[0] = pd.cc_role_ctrl;
		return 1;
	case TCPC_REG_TCPC_CTRL:
		payload[0] = TCPC_REG_TCPC_CTRL_SET(pd.polarity);
		return 1;
	case TCPC_REG_MSG_HDR_INFO:
		payload[0] = TCPC_REG_MSG_HDR_INFO_SET(pd.data_role, pd.power_role);
		return 1;
	case TCPC_REG_RX_DETECT:
		payload[0] = pd.rx_enabled ? TCPC_REG_RX_DETECT_SOP_HRST_MASK : 0;
		return 1;

	case TCPC_REG_RX_BYTE_CNT:
		payload[0] = 3 + 4 *
			PD_HEADER_CNT(pd.rx_head[pd.rx_buf_tail]);
		return 1;
	case TCPC_REG_RX_HDR:
		payload[0] = pd.rx_head[pd.rx_buf_tail] & 0xff;
		payload[1] = (pd.rx_head[pd.rx_buf_tail] >> 8) & 0xff;
		return 2;
	case TCPC_REG_RX_DATA:
		memcpy(payload, pd.rx_payload[pd.rx_buf_tail],
		       sizeof(pd.rx_payload[pd.rx_buf_tail]));
		return sizeof(pd.rx_payload[pd.rx_buf_tail]);
	case TCPC_REG_POWER_STATUS:
		payload[0] = pd.power_status;
		return 1;

	case TCPC_REG_TX_HDR:
		payload[0] = pd.tx_head & 0xff;
		payload[1] = (pd.tx_head >> 8) & 0xff;
		return 2;
	case TCPC_REG_TX_DATA:
		memcpy(payload, pd.tx_payload,
		       sizeof(pd.tx_payload));
		return sizeof(pd.tx_payload);
	default:
		return 0;
	}
}

void tcpc_i2c_process(uint8_t read, uint32_t len, uint8_t *payload)
{
	/* length must always be at least 1 */
	if (len == 0) {
		/*
		 * if this is a read, we must call send_response() for
		 * i2c transaction to finishe properly
		 */
		//if (read)
			//(*send_response)(0);
	}

	/* if this is a write, length must be at least 2 */
	if (!read && len < 2)
		return;

	/* perform read or write */
	if (read) {
		len = tcpc_i2c_read(payload);
		//(*send_response)(len);
	} else {
		tcpc_i2c_write(len, payload);
	}
}

static inline void tcpc_detect_cc_status(void)
{
	uint8_t cc1 = pd_cc_read_status(1, TCPC_REG_ROLE_CTRL_CC1(pd.cc_role_ctrl), TCPC_REG_ROLE_CTRL_RP(pd.cc_role_ctrl));
	uint8_t cc2 = pd_cc_read_status(2, TCPC_REG_ROLE_CTRL_CC2(pd.cc_role_ctrl), TCPC_REG_ROLE_CTRL_RP(pd.cc_role_ctrl));

	if (pd.cc_status[0] != cc1 || pd.cc_status[1] != cc2) {
		pd.cc_status[0] = cc1;
		pd.cc_status[1] = cc2;
		alert(TCPC_REG_ALERT_CC_STATUS);
	}
}

// Run TCPC state machine once
void tcpc_run(void)
{
	uint64_t cur_timestamp = timestamp_get();

	// Check CC lines every 1mS
	if (cur_timestamp > pd.cc_last_sampled_timestamp + 1000)
	{
		pd.cc_last_sampled_timestamp = cur_timestamp;

		// Check CC lines
		tcpc_detect_cc_status();
	}
}
