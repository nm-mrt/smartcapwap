#include "wtp.h"
#include "capwap_network.h"
#include "capwap_protocol.h"
#include "capwap_dfa.h"
#include "capwap_array.h"
#include "capwap_list.h"
#include "capwap_element.h"
#include "capwap_dtls.h"
#include "wtp_dfa.h"
#include "wtp_radio.h"

#include <arpa/inet.h>
#include <ev.h>
#include <libconfig.h>

struct wtp_t g_wtp;

/* Local param */
#define WTP_STANDARD_NAME						"Unknown WTP"
#define WTP_STANDARD_LOCATION					"Unknown Location"
#define WTP_RADIO_INITIALIZATION_INTERVAL		1000

static char g_configurationfile[260] = WTP_STANDARD_CONFIGURATION_FILE;

/* Alloc WTP */
static int wtp_init(void)
{
	/* Init WTP with default value */
	memset(&g_wtp, 0, sizeof(struct wtp_t));

	/* Standard running mode is standalone */
	g_wtp.standalone = 1;
	strcpy(g_wtp.wlanprefix, WTP_PREFIX_DEFAULT_NAME);

	/* Standard name */
	g_wtp.name.name = (uint8_t*)capwap_duplicate_string(WTP_STANDARD_NAME);
	g_wtp.location.value = (uint8_t*)capwap_duplicate_string(WTP_STANDARD_LOCATION);

	/* State machine */
	g_wtp.state = CAPWAP_START_STATE;
	g_wtp.discoveryinterval = WTP_DISCOVERY_INTERVAL;
	g_wtp.echointerval = WTP_ECHO_INTERVAL;

	/* Socket */
	capwap_network_init(&g_wtp.net);

	/* Standard configuration */
	g_wtp.boarddata.boardsubelement =
		capwap_array_create(sizeof(struct capwap_wtpboarddata_board_subelement), 0, 1);
	g_wtp.descriptor.encryptsubelement =
		capwap_array_create(sizeof(struct capwap_wtpdescriptor_encrypt_subelement), 0, 0);
	g_wtp.descriptor.descsubelement =
		capwap_array_create(sizeof(struct capwap_wtpdescriptor_desc_subelement), 0, 1);

	g_wtp.binding = CAPWAP_WIRELESS_BINDING_NONE;

	g_wtp.ecn.flag = CAPWAP_LIMITED_ECN_SUPPORT;
	g_wtp.transport.type = CAPWAP_UDP_TRANSPORT;
	g_wtp.statisticstimer.timer = WTP_STATISTICSTIMER_INTERVAL / 1000;

	g_wtp.mactype.type = CAPWAP_LOCALMAC;
	g_wtp.mactunnel.mode = CAPWAP_WTP_LOCAL_BRIDGING;

	/* DTLS */
	g_wtp.validdtlsdatapolicy = CAPWAP_ACDESC_CLEAR_DATA_CHANNEL_ENABLED;

	/* Tx fragment packets */
	g_wtp.requestfragmentpacket = capwap_list_create();
	g_wtp.responsefragmentpacket = capwap_list_create();

	wtp_reset_state();

	/* AC information */
	g_wtp.discoverytype.type = CAPWAP_DISCOVERYTYPE_TYPE_UNKNOWN;
	g_wtp.acdiscoveryrequest = 1;
	g_wtp.acdiscoveryarray = capwap_array_create(sizeof(struct addr_capwap), 0, 0);
	g_wtp.acpreferedarray = capwap_array_create(sizeof(struct addr_capwap), 0, 0);
	g_wtp.acdiscoveryresponse = capwap_array_create(sizeof(struct wtp_discovery_response), 0, 1);

	/* Radios */
	wtp_radio_init();

	return 1;
}

/* Destroy WTP */
static void wtp_destroy(void) {
	int i;

	/* Dtls */
	capwap_crypt_freecontext(&g_wtp.dtlscontext);

	/* Free standard configuration */
	capwap_array_free(g_wtp.descriptor.encryptsubelement);

	for (i = 0; i < g_wtp.descriptor.descsubelement->count; i++) {
		struct capwap_wtpdescriptor_desc_subelement* element = (struct capwap_wtpdescriptor_desc_subelement*)capwap_array_get_item_pointer(g_wtp.descriptor.descsubelement, i);

		if (element->data) {
			capwap_free(element->data);
		}
	}

	for (i = 0; i < g_wtp.boarddata.boardsubelement->count; i++) {
		struct capwap_wtpboarddata_board_subelement* element = (struct capwap_wtpboarddata_board_subelement*)capwap_array_get_item_pointer(g_wtp.boarddata.boardsubelement, i);

		if (element->data) {
			capwap_free(element->data);
		}
	}

	capwap_array_free(g_wtp.descriptor.descsubelement);
	capwap_array_free(g_wtp.boarddata.boardsubelement);

	/* Free fragments packet */
	capwap_list_free(g_wtp.requestfragmentpacket);
	capwap_list_free(g_wtp.responsefragmentpacket);

	/* Free list AC */
	capwap_array_free(g_wtp.acdiscoveryarray);
	capwap_array_free(g_wtp.acpreferedarray);

	wtp_free_discovery_response_array();
	capwap_array_free(g_wtp.acdiscoveryresponse);

	/* Free local message elements */
	capwap_free(g_wtp.name.name);
	capwap_free(g_wtp.location.value);

	/* Free radios */
	wtp_radio_free();
}

/* */
static void wtp_add_default_acaddress() {
	struct addr_capwap address;

	/* Broadcast IPv4 */
	memset(&address, 0, sizeof(struct addr_capwap));
	address.resolved = 1;
	address.sockaddr.sin.sin_family = AF_INET;
	address.sockaddr.sin.sin_addr.s_addr = INADDR_BROADCAST;
	address.sockaddr.sin.sin_port = htons(CAPWAP_CONTROL_PORT);
	memcpy(capwap_array_get_item_pointer(g_wtp.acdiscoveryarray, g_wtp.acdiscoveryarray->count), &address, sizeof(struct addr_capwap));

	/* Multicast IPv4 */
	/* TODO */

	/* Multicast IPv6 */
	/* TODO */
}

/* Help */
static void wtp_print_usage(void) {
	/* TODO */
}

static int wtp_parsing_radio_qos_configuration(config_setting_t *elem, const char *section,
					       struct capwap_80211_wtpqos_subelement *qos)
{
	config_setting_t *sect;
	LIBCONFIG_LOOKUP_INT_ARG val;

	sect = config_setting_get_member(elem, section);
	if (!sect)
		return 0;

	if (config_setting_lookup_int(sect, "queuedepth", &val) != CONFIG_TRUE ||
	    val > 255)
		return 0;
	qos->queuedepth = val;

	if (config_setting_lookup_int(sect, "cwmin", &val) != CONFIG_TRUE ||
	    val > 65565)
		return 0;
	qos->cwmin = val;

	if (config_setting_lookup_int(sect, "cwmax", &val) != CONFIG_TRUE ||
	    val > 65565)
		return 0;
	qos->cwmax = val;

	if (config_setting_lookup_int(sect, "aifs", &val) != CONFIG_TRUE ||
	    val > 255)
		return 0;
	qos->aifs = val;

	if (config_setting_lookup_int(sect, "priority8021p", &val) != CONFIG_TRUE ||
	    val > 7)
		return 0;
	qos->priority8021p = val;

	if (config_setting_lookup_int(sect, "dscp", &val) != CONFIG_TRUE ||
	    val > 255)
		return 0;
	qos->dscp = val;

	return 1;
}

/* */
static int wtp_parsing_radio_80211n_cfg(config_setting_t *elem,
					struct wtp_radio *radio)
{
	config_setting_t* sect;
	int boolval;
	LIBCONFIG_LOOKUP_INT_ARG intval;

	sect = config_setting_get_member(elem, "ieee80211n");
	if (!sect) {
		log_printf(LOG_ERR, "application.radio.ieee80211n not found");
		return 0;
	}

	radio->n_radio_cfg.radioid = radio->radioid;

	if (config_setting_lookup_bool(sect, "a-msdu", &boolval) != CONFIG_TRUE) {
		log_printf(LOG_ERR, "application.radio.ieee80211n.a-msdu not found or wrong type");
		return 0;
	}
	if (boolval)
		radio->n_radio_cfg.flags |= CAPWAP_80211N_RADIO_CONF_A_MSDU;

	if (config_setting_lookup_bool(sect, "a-mpdu", &boolval) != CONFIG_TRUE) {
		log_printf(LOG_ERR, "application.radio.ieee80211n.a-mpdu not found or wrong type");
		return 0;
	}
	if (boolval)
		radio->n_radio_cfg.flags |= CAPWAP_80211N_RADIO_CONF_A_MPDU;

	if (config_setting_lookup_bool(sect, "require-ht", &boolval) != CONFIG_TRUE) {
		log_printf(LOG_ERR, "application.radio.ieee80211n.require-ht not found or wrong type");
		return 0;
	}
	if (boolval)
		radio->n_radio_cfg.flags |= CAPWAP_80211N_RADIO_CONF_11N_ONLY;

	if (config_setting_lookup_bool(sect, "short-gi", &boolval) != CONFIG_TRUE) {
		log_printf(LOG_ERR, "application.radio.ieee80211n.short-gi not found or wrong type");
		return 0;
	}
	if (boolval)
		radio->n_radio_cfg.flags |= CAPWAP_80211N_RADIO_CONF_SHORT_GUARD_INTERVAL;

	if (config_setting_lookup_bool(sect, "ht40", &boolval) != CONFIG_TRUE) {
		log_printf(LOG_ERR, "application.radio.ieee80211n.ht40 not found or wrong type");
		return 0;
	}
	if (!boolval)
		radio->n_radio_cfg.flags |= CAPWAP_80211N_RADIO_CONF_20MHZ_BANDWITH;

	if (config_setting_lookup_int(sect, "max-sup-mcs", &intval) != CONFIG_TRUE) {
		log_printf(LOG_ERR, "application.radio.ieee80211n.max-sup-mcs not found or wrong type");
		return 0;
	}
	radio->n_radio_cfg.maxsupmcs = intval;

	if (config_setting_lookup_int(sect, "max-mand-mcs", &intval) != CONFIG_TRUE) {
		log_printf(LOG_ERR, "application.radio.ieee80211n.max-mand-mcs not found or wrong type");
		return 0;
	}
	radio->n_radio_cfg.maxmandmcs = intval;

	if (config_setting_lookup_int(sect, "tx-antenna", &intval) != CONFIG_TRUE) {
		log_printf(LOG_ERR, "application.radio.ieee80211n.tx-antenna not found or wrong type");
		return 0;
	}
	radio->n_radio_cfg.txant = intval;

	if (config_setting_lookup_int(sect, "rx-antenna", &intval) != CONFIG_TRUE) {
		log_printf(LOG_ERR, "application.radio.ieee80211n.rx-antenna not found or wrong type");
		return 0;
	}
	radio->n_radio_cfg.rxant = intval;

	return 1;
}

/* */
static int wtp_parsing_radio_configuration(config_setting_t* configElement, struct wtp_radio* radio) {
	int i, len, cnt;
	int configBool;
	LIBCONFIG_LOOKUP_INT_ARG configInt;
	const char* configString;
	config_setting_t* configItems;
	config_setting_t* configSection;

	/* Physical radio mode */
	radio->radioinformation.radioid = radio->radioid;
	if (config_setting_lookup_string(configElement, "mode", &configString) != CONFIG_TRUE)
		return 0;

	len = strlen(configString);
	if (!len)
		return 0;

	for (i = 0; i < len; i++) {
		switch (configString[i]) {
		case 'a':
			radio->radioinformation.radiotype |= CAPWAP_RADIO_TYPE_80211A;
			break;

		case 'b':
			radio->radioinformation.radiotype |= CAPWAP_RADIO_TYPE_80211B;
			break;

		case 'g':
			radio->radioinformation.radiotype |= CAPWAP_RADIO_TYPE_80211G;
			break;

		case 'n':
			radio->radioinformation.radiotype |= CAPWAP_RADIO_TYPE_80211N;
			break;

		default:
			return 0;
		}
	}

	/* Antenna */
	configSection = config_setting_get_member(configElement, "antenna");
	if (!configSection)
		return 0;

	radio->antenna.radioid = radio->radioid;

	if (config_setting_lookup_bool(configSection, "diversity", &configBool) != CONFIG_TRUE)
		return 0;
	radio->antenna.diversity = (configBool ? CAPWAP_ANTENNA_DIVERSITY_ENABLE : CAPWAP_ANTENNA_DIVERSITY_DISABLE);

	if (config_setting_lookup_string(configSection, "combiner", &configString) != CONFIG_TRUE)
		return 0;
	if (!strcmp(configString, "left")) {
		radio->antenna.combiner = CAPWAP_ANTENNA_COMBINER_SECT_LEFT;
	} else if (!strcmp(configString, "right")) {
		radio->antenna.combiner = CAPWAP_ANTENNA_COMBINER_SECT_RIGHT;
	} else if (!strcmp(configString, "omni")) {
		radio->antenna.combiner = CAPWAP_ANTENNA_COMBINER_SECT_OMNI;
	} else if (!strcmp(configString, "mimo")) {
		radio->antenna.combiner = CAPWAP_ANTENNA_COMBINER_SECT_MIMO;
	} else {
		return 0;
	}

	configItems = config_setting_get_member(configSection, "selection");
	if (!configItems)
		return 0;

	cnt = config_setting_length(configItems);
	if (cnt == 0 || cnt > CAPWAP_ANTENNASELECTIONS_MAXLENGTH)
		return 0;

	for (i = 0; i < cnt; i++) {
		uint8_t* selection = (uint8_t*)capwap_array_get_item_pointer(radio->antenna.selections, i);

		configString = config_setting_get_string_elem(configItems, i);
		if (!strcmp(configString, "internal")) {
			*selection = CAPWAP_ANTENNA_INTERNAL;
		} else if (!strcmp(configString, "external")) {
			*selection = CAPWAP_ANTENNA_EXTERNAL;
		} else {
			return 0;
		}
	}

	/* Multi-Domain Capability */
	configSection = config_setting_get_member(configElement, "multidomaincapability");
	if (configSection) {
		radio->multidomaincapability.radioid = radio->radioid;

		if (config_setting_lookup_int(configSection, "firstchannel", &configInt) != CONFIG_TRUE ||
		    configInt == 0 || configInt > 65535)
			return 0;
		radio->multidomaincapability.firstchannel = (uint16_t)configInt;

		if (config_setting_lookup_int(configSection, "numberchannels", &configInt) != CONFIG_TRUE ||
		    configInt == 0 || configInt > 65535)
			return 0;
		radio->multidomaincapability.numberchannels = (uint16_t)configInt;

		if (config_setting_lookup_int(configSection, "maxtxpower", &configInt) != CONFIG_TRUE ||
		    configInt == 0 || configInt > 65535)
			return 0;
		radio->multidomaincapability.maxtxpowerlevel = (uint16_t)configInt;
	}

	/* MAC Operation */
	radio->macoperation.radioid = radio->radioid;

	if (config_setting_lookup_int(configElement, "rtsthreshold", &configInt) != CONFIG_TRUE ||
	    configInt == 0 || configInt > 2347)
		return 0;
	radio->macoperation.rtsthreshold = (uint16_t)configInt;

	if (config_setting_lookup_int(configElement, "shortretry", &configInt) != CONFIG_TRUE ||
	    configInt < 2 || configInt > 255)
		return 0;
	radio->macoperation.shortretry = (uint8_t)configInt;

	if (config_setting_lookup_int(configElement, "longretry", &configInt) != CONFIG_TRUE ||
	    configInt < 2 || configInt > 255)
		return 0;
	radio->macoperation.longretry = (uint8_t)configInt;

	if (config_setting_lookup_int(configElement, "fragmentationthreshold", &configInt) != CONFIG_TRUE ||
	    configInt < 256 || configInt > 2346)
		return 0;
	radio->macoperation.fragthreshold = (uint16_t)configInt;

	if (config_setting_lookup_int(configElement, "txmsdulifetime", &configInt) != CONFIG_TRUE ||
	    configInt == 0)
		return 0;
	radio->macoperation.txmsdulifetime = (uint32_t)configInt;

	if (config_setting_lookup_int(configElement, "rxmsdulifetime", &configInt) != CONFIG_TRUE ||
	    configInt == 0)
		return 0;
	radio->macoperation.rxmsdulifetime = (uint32_t)configInt;

	/* Supported rate */
	radio->supportedrates.radioid = radio->radioid;

	configItems = config_setting_get_member(configElement, "supportedrates");
	if (!configItems)
		return 0;

	cnt = config_setting_length(configItems);
	if (cnt < CAPWAP_SUPPORTEDRATES_MINLENGTH ||
	    cnt > CAPWAP_SUPPORTEDRATES_MAXLENGTH)
		return 0;

	radio->supportedrates.supportedratescount = (uint8_t)cnt;
	for (i = 0; i < cnt; i++) {
		config_setting_t *elem;
		int value;

		elem = config_setting_get_elem(configItems, i);
		switch (config_setting_type(elem)) {
		case CONFIG_TYPE_INT:
			value = config_setting_get_int(elem) * 2;
			break;
		case CONFIG_TYPE_FLOAT:
			value = config_setting_get_float(elem) * 2;
			break;
		default:
			return 0;
		}
		if (value < 2 || value > 127)
			return 0;
		radio->supportedrates.supportedrates[i] = (uint8_t)value;
	}

	/* TX Power */
	configSection = config_setting_get_member(configElement, "txpower");
	if (configSection) {
		radio->txpower.radioid = radio->radioid;

		if (config_setting_lookup_int(configSection, "current", &configInt) != CONFIG_TRUE ||
		    configInt == 0 || configInt > 10000)
			return 0;
		radio->txpower.currenttxpower = (uint16_t)configInt;

		configItems = config_setting_get_member(configSection, "supported");
		if (configItems != NULL) {
			cnt = config_setting_length(configItems);
			if (cnt == 0 || cnt > CAPWAP_TXPOWERLEVEL_MAXLENGTH)
				return 0;

			radio->txpowerlevel.radioid = radio->radioid;
			radio->txpowerlevel.numlevels = (uint8_t)cnt;

			for (i = 0; i < cnt; i++) {
				int value = config_setting_get_int_elem(configItems, i);
				if (value < 0 || value > 10000)
					return 0;
				radio->txpowerlevel.powerlevel[i] = (uint8_t)value;
			}
		}
	}

	/* WTP Radio Configuration */
	radio->radioconfig.radioid = radio->radioid;

	if (config_setting_lookup_bool(configElement, "shortpreamble", &configBool) != CONFIG_TRUE)
		return 0;
	radio->radioconfig.shortpreamble = (configBool ? CAPWAP_WTP_RADIO_CONF_SHORTPREAMBLE_ENABLE : CAPWAP_WTP_RADIO_CONF_SHORTPREAMBLE_DISABLE);

	if (config_setting_lookup_int(configElement, "maxbssid", &configInt) != CONFIG_TRUE ||
	    configInt == 0 || configInt > 16)
		return 0;
	radio->radioconfig.maxbssid = (uint8_t)configInt;

	if (config_setting_lookup_string(configElement, "bssprefixname", &configString) != CONFIG_TRUE ||
	    strlen(configString) >= IFNAMSIZ)
		return 0;
	strcpy(radio->wlanprefix, configString);

	if (config_setting_lookup_int(configElement, "dtimperiod", &configInt) != CONFIG_TRUE ||
	    configInt == 0 || configInt > 256)
		return 0;
	radio->radioconfig.dtimperiod = (uint8_t)configInt;

	if (config_setting_lookup_int(configElement, "beaconperiod", &configInt) != CONFIG_TRUE ||
	    configInt == 0 ||configInt > 65535)
		return 0;
	radio->radioconfig.beaconperiod = (uint16_t)configInt;

	if (config_setting_lookup_string(configElement, "country", &configString) != CONFIG_TRUE ||
	    strlen(configString) != 2)
		return 0;

	radio->radioconfig.country[0] = (uint8_t)configString[0];
	radio->radioconfig.country[1] = (uint8_t)configString[1];

	if (config_setting_lookup_bool(configElement, "shortpreamble", &configBool) == CONFIG_TRUE)
		radio->radioconfig.country[2] = (uint8_t)(configBool ? 'O' : 'I');
	else
		radio->radioconfig.country[2] = (uint8_t)' ';

	/* QoS */
	radio->qos.radioid = radio->radioid;

	configSection = config_setting_get_member(configElement, "qos");
	if (!configSection)
		return 0;

	if (config_setting_lookup_int(configSection, "taggingpolicy", &configInt) != CONFIG_TRUE ||
	    configInt > 255)
		return 0;
	radio->qos.taggingpolicy = (uint8_t)configInt;

	if (wtp_parsing_radio_qos_configuration(configSection, "voice", &radio->qos.qos[3]) == 0 ||
	    wtp_parsing_radio_qos_configuration(configSection, "video", &radio->qos.qos[2]) == 0 ||
	    wtp_parsing_radio_qos_configuration(configSection, "besteffort", &radio->qos.qos[0]) == 0 ||
	    wtp_parsing_radio_qos_configuration(configSection, "background", &radio->qos.qos[1]) == 0)
		return 0;

	if (radio->radioinformation.radiotype & CAPWAP_RADIO_TYPE_80211N) {
		if (wtp_parsing_radio_80211n_cfg(configElement, radio) == 0)
			return 0;
	}

	return 1;
}


/* */
static int wtp_parsing_radio_section_configuration(config_setting_t* configSetting)
{
	int i;
	int configBool;
	const char* configString;
	struct wtp_radio* radio;
	const struct wifi_capability* capability;
	int count = config_setting_length(configSetting);

	if (g_wtp.binding != CAPWAP_WIRELESS_BINDING_IEEE80211)
		return 1;

	for (i = 0; i < count; i++) {
		if (!IS_VALID_RADIOID(g_wtp.radios->count + 1)) {
			log_printf(LOG_ERR, "Exceeded max number of radio device");
			return 0;
		}

		/* */
		config_setting_t* configElement = config_setting_get_elem(configSetting, i);
		if (!configElement)
			continue;

		if (config_setting_lookup_string(configElement, "device", &configString) != CONFIG_TRUE) {
			log_printf(LOG_ERR, "Invalid configuration file, element application.radio.device not found");
			return 0;
		}

		if (*configString && (strlen(configString) >= IFNAMSIZ)) {
			log_printf(LOG_ERR, "Invalid configuration file, application.radio.device string length exceeded");
			return 0;
		}

		/* Create new radio device */
		radio = wtp_radio_create_phy();
		strcpy(radio->device, configString);

		if (config_setting_lookup_bool(configElement, "enabled", &configBool) != CONFIG_TRUE
		    || !configBool)
			continue;

		/* Retrieve radio capability */
		if (wtp_parsing_radio_configuration(configElement, radio) == 0) {
			log_printf(LOG_ERR, "Invalid configuration file, application.radio");
			return 0;
		}

		/* Initialize radio device */
		if (config_setting_lookup_string(configElement, "driver", &configString) != CONFIG_TRUE ||
		    !*configString ||
		    (strlen(configString) >= WIFI_DRIVER_NAME_SIZE))
			continue;

		radio->devicehandle = wifi_device_connect(radio->device, configString);
		if (!radio->devicehandle) {
			radio->status = WTP_RADIO_HWFAILURE;
			log_printf(LOG_WARNING, "Unable to register radio device: %s - %s", radio->device, configString);
		}

		radio->status = WTP_RADIO_ENABLED;
		log_printf(LOG_INFO, "Register radioid %d with radio device: %s - %s", radio->radioid, radio->device, configString);

		/* Update radio capability with device query */
		capability = wifi_device_getcapability(radio->devicehandle);
		if (!capability)
			continue;

		uint8_t bssid;

		/* Create interface */
		for (bssid = 0; bssid < radio->radioconfig.maxbssid; bssid++) {
			char wlanname[IFNAMSIZ];
			struct wtp_radio_wlan *wlan;

			sprintf(wlanname, "%s%02d.%02d", radio->wlanprefix, (int)radio->radioid, (int)bssid + 1);
			if (wifi_iface_index(wlanname)) {
				log_printf(LOG_ERR, "interface %s already exists", wlanname);
				return 0;
			}

			/* */
			wlan = (struct wtp_radio_wlan *)capwap_array_get_item_pointer(radio->wlan, bssid + 1);
			wlan->in_use = 0;
			wlan->radio = radio;
			wlan->wlanhandle = wifi_wlan_create(radio->devicehandle, wlanname);
			if (!wlan->wlanhandle) {
				log_printf(LOG_ERR, "Unable to create interface: %s", wlanname);
				return 0;
			}

			log_printf(LOG_DEBUG, "Created wlan interface: %s", wlanname);
		}
	}

	/* Update radio status */
	g_wtp.descriptor.maxradios = g_wtp.radios->count;
	g_wtp.descriptor.radiosinuse = wtp_update_radio_in_use();

	return 1;
}

/* Parsing boardinfo configuration */
static int wtp_parsing_cfg_boardinfo_element(config_t *config)
{
	config_setting_t *configSetting;
	int count, i;

	/* Set Element Boardinfo of WTP */
	configSetting = config_lookup(config, "application.boardinfo.element");
	if (!configSetting)
		return 1;

	count = config_setting_length(configSetting);
	for (i = 0; i < count; i++) {
		config_setting_t *configElement;
		const char *configName;
		const char *configValue;
		int lengthValue;
		struct capwap_wtpboarddata_board_subelement *element;

		configElement = config_setting_get_elem(configSetting, i);
		if (!configElement)
			continue;

		if (config_setting_lookup_string(configElement, "name", &configName) != CONFIG_TRUE) {
			log_printf(LOG_ERR, "Invalid configuration file, element application.boardinfo.element.name not found");
			return 0;
		}

		if (config_setting_lookup_string(configElement, "value", &configValue) != CONFIG_TRUE) {
			log_printf(LOG_ERR, "Invalid configuration file, element application.boardinfo.element.value not found");
			return 0;
		}

		lengthValue = strlen(configValue);
		if (lengthValue >= CAPWAP_BOARD_SUBELEMENT_MAXDATA) {
			log_printf(LOG_ERR, "Invalid configuration file, application.boardinfo.element.value string length exceeded");
			return 0;
		}

		element = (struct capwap_wtpboarddata_board_subelement*)capwap_array_get_item_pointer(g_wtp.boarddata.boardsubelement, g_wtp.boarddata.boardsubelement->count);

		if (strcmp(configName, "model") == 0) {
			element->type = CAPWAP_BOARD_SUBELEMENT_MODELNUMBER;
			element->length = lengthValue;
			element->data = (uint8_t*)capwap_clone((void*)configValue, lengthValue);
		} else if (strcmp(configName, "serial") == 0) {
			element->type = CAPWAP_BOARD_SUBELEMENT_SERIALNUMBER;
			element->length = lengthValue;
			element->data = (uint8_t*)capwap_clone((void*)configValue, lengthValue);
		} else if (strcmp(configName, "id") == 0) {
			element->type = CAPWAP_BOARD_SUBELEMENT_ID;
			element->length = lengthValue;
			element->data = (uint8_t*)capwap_clone((void*)configValue, lengthValue);
		} else if (strcmp(configName, "revision") == 0) {
			element->type = CAPWAP_BOARD_SUBELEMENT_REVISION;
			element->length = lengthValue;
			element->data = (uint8_t*)capwap_clone((void*)configValue, lengthValue);
		} else if (strcmp(configName, "macaddress") == 0) {
			const char* configType;

			if (config_setting_lookup_string(configElement, "type", &configType) != CONFIG_TRUE) {
				log_printf(LOG_ERR, "Invalid configuration file, element application.boardinfo.element.type not found");
				return 0;
			}

			if (strcmp(configType, "interface") == 0) {
				char macaddress[MACADDRESS_EUI64_LENGTH];

				/* Retrieve macaddress */
				element->type = CAPWAP_BOARD_SUBELEMENT_MACADDRESS;
				element->length = capwap_get_macaddress_from_interface(configValue, macaddress);
				if (!element->length || ((element->length != MACADDRESS_EUI64_LENGTH) && (element->length != MACADDRESS_EUI48_LENGTH))) {
					log_printf(LOG_ERR, "Invalid configuration file, unable found macaddress of interface: '%s'", configValue);
					return 0;
				}

				element->data = (uint8_t*)capwap_clone((void*)macaddress, element->length);
			} else {
				log_printf(LOG_ERR, "Invalid configuration file, unknown application.boardinfo.element.type value");
				return 0;
			}
		} else {
			log_printf(LOG_ERR, "Invalid configuration file, unknown application.boardinfo.element.name value");
			return 0;
		}
	}

	return 1;
}

/* Set info descriptor of WTP */
static int wtp_parsing_cfg_descriptor_info(config_t *config)
{
	config_setting_t *configSetting;
	int count, i;

	configSetting = config_lookup(config, "application.descriptor.info");
	if (!configSetting)
		return 1;

	count = config_setting_length(configSetting);
	for (i = 0; i < count; i++) {
		config_setting_t *configElement;
		LIBCONFIG_LOOKUP_INT_ARG configVendor;
		const char *configType;
		const char *configValue;
		int lengthValue;
		unsigned short type;
		struct capwap_wtpdescriptor_desc_subelement *desc;

		configElement = config_setting_get_elem(configSetting, i);
		if (!configElement)
			continue;

		if (config_setting_lookup_int(configElement, "idvendor", &configVendor) != CONFIG_TRUE) {
			log_printf(LOG_ERR, "Invalid configuration file, element application.descriptor.info.idvendor not found");
			return 0;
		}

		if (config_setting_lookup_string(configElement, "type", &configType) != CONFIG_TRUE) {
			log_printf(LOG_ERR, "Invalid configuration file, element application.descriptor.info.type not found");
			return 0;
		}

		if (config_setting_lookup_string(configElement, "value", &configValue) != CONFIG_TRUE) {
			log_printf(LOG_ERR, "Invalid configuration file, element application.descriptor.info.value not found");
			return 0;
		}

		lengthValue = strlen(configValue);
		if (lengthValue >= CAPWAP_WTPDESC_SUBELEMENT_MAXDATA) {
			log_printf(LOG_ERR, "Invalid configuration file, application.descriptor.info.value string length exceeded");
			return 0;
		}

		if (!strcmp(configType, "hardware")) {
			type = CAPWAP_WTPDESC_SUBELEMENT_HARDWAREVERSION;
		} else if (!strcmp(configType, "software")) {
			type = CAPWAP_WTPDESC_SUBELEMENT_SOFTWAREVERSION;
		} else if (!strcmp(configType, "boot")) {
			type = CAPWAP_WTPDESC_SUBELEMENT_BOOTVERSION;
		} else if (!strcmp(configType, "other")) {
			type = CAPWAP_WTPDESC_SUBELEMENT_OTHERVERSION;
		} else {
			log_printf(LOG_ERR, "Invalid configuration file, unknown application.descriptor.info.type value");
			return 0;
		}

		desc = (struct capwap_wtpdescriptor_desc_subelement*)capwap_array_get_item_pointer(g_wtp.descriptor.descsubelement, g_wtp.descriptor.descsubelement->count);
		desc->vendor = (unsigned long)configVendor;
		desc->type = type;
		desc->data = (uint8_t*)capwap_duplicate_string(configValue);
	}

	return 1;
}

/* Parsing configuration */
static int wtp_parsing_configuration_1_0(config_t* config) {
	int i;
	int configBool;
	LIBCONFIG_LOOKUP_INT_ARG configInt;
	const char* configString;
	config_setting_t* configSetting;

	/* Logging configuration */
	if (config_lookup_bool(config, "logging.enable", &configBool) == CONFIG_TRUE) {
		if (!configBool) {
			capwap_logging_verboselevel(LOG_NONE);
			capwap_logging_disable_allinterface();
		} else {
			if (config_lookup_string(config, "logging.level", &configString) == CONFIG_TRUE) {
				if (!strcmp(configString, "fatal")) {
					capwap_logging_verboselevel(LOG_EMERG);
				} else if (!strcmp(configString, "error")) {
					capwap_logging_verboselevel(LOG_ERR);
				} else if (!strcmp(configString, "warning")) {
					capwap_logging_verboselevel(LOG_WARNING);
				} else if (!strcmp(configString, "info")) {
					capwap_logging_verboselevel(LOG_INFO);
				} else if (!strcmp(configString, "debug")) {
					capwap_logging_verboselevel(LOG_DEBUG);
				} else {
					log_printf(LOG_ERR, "Invalid configuration file, unknown logging.level value");
					return 0;
				}
			}

			/* Logging output interface */
			configSetting = config_lookup(config, "logging.output");
			if (configSetting != NULL) {
				int count = config_setting_length(configSetting);

				/* Disable output interface */
				capwap_logging_disable_allinterface();

				/* Enable selected interface */
				for (i = 0; i < count; i++) {
					config_setting_t* configElement = config_setting_get_elem(configSetting, i);
					if ((configElement != NULL) && (config_setting_lookup_string(configElement, "mode", &configString) == CONFIG_TRUE)) {
						if (!strcmp(configString, "stdout")) {
							capwap_logging_enable_console(0);
						} else if (!strcmp(configString, "stderr")) {
							capwap_logging_enable_console(1);
						} else {
							log_printf(LOG_ERR, "Invalid configuration file, unknown logging.output value");
							return 0;
						}
					}
				}
			}
		}
	}

	/* Set running mode */
	if (config_lookup_bool(config, "application.standalone", &configBool) == CONFIG_TRUE) {
		g_wtp.standalone = ((configBool != 0) ? 1 : 0);
	}

	/* Set name of WTP */
	if (config_lookup_string(config, "application.name", &configString) == CONFIG_TRUE) {
		if (strlen(configString) > CAPWAP_WTPNAME_MAXLENGTH) {
			log_printf(LOG_ERR, "Invalid configuration file, application.name string length exceeded");
			return 0;
		}

		capwap_free(g_wtp.name.name);
		g_wtp.name.name = (uint8_t*)capwap_duplicate_string(configString);
	}

	/* Set location of WTP */
	if (config_lookup_string(config, "application.location", &configString) == CONFIG_TRUE) {
		if (strlen(configString) > CAPWAP_LOCATION_MAXLENGTH) {
			log_printf(LOG_ERR, "Invalid configuration file, application.location string length exceeded");
			return 0;
		}

		capwap_free(g_wtp.location.value);
		g_wtp.location.value = (uint8_t*)capwap_duplicate_string(configString);
	}

	/* Set binding of WTP */
	if (config_lookup_string(config, "application.binding", &configString) == CONFIG_TRUE) {
		if (!strcmp(configString, "802.11")) {
			g_wtp.binding = CAPWAP_WIRELESS_BINDING_IEEE80211;
		} else if (!strcmp(configString, "EPCGlobal")) {
			g_wtp.binding = CAPWAP_WIRELESS_BINDING_EPCGLOBAL;
		} else {
			log_printf(LOG_ERR, "Invalid configuration file, unknown application.binding value");
			return 0;
		}
	}

	/* Initialize binding */
	switch (g_wtp.binding) {
		case CAPWAP_WIRELESS_BINDING_NONE: {
			break;
		}

		case CAPWAP_WIRELESS_BINDING_IEEE80211: {
			/* Initialize wifi binding driver */
			log_printf(LOG_INFO, "Initializing wifi binding engine");
			if (wifi_driver_init()) {
				log_printf(LOG_EMERG, "Unable initialize wifi binding engine");
				return 0;
			}

			break;
		}

		default: {
			log_printf(LOG_EMERG, "Unable initialize unknown binding engine: %hu", g_wtp.binding);
			return 0;
		}
	}

	/* Set tunnelmode of WTP */
	if (config_lookup(config, "application.tunnelmode") != NULL) {
		g_wtp.mactunnel.mode = 0;
		if (config_lookup_bool(config, "application.tunnelmode.nativeframe", &configBool) == CONFIG_TRUE) {
			if (configBool != 0) {
				g_wtp.mactunnel.mode |= CAPWAP_WTP_NATIVE_FRAME_TUNNEL;
			}
		}

		if (config_lookup_bool(config, "application.tunnelmode.ethframe", &configBool) == CONFIG_TRUE) {
			if (configBool != 0) {
				g_wtp.mactunnel.mode |=  CAPWAP_WTP_8023_FRAME_TUNNEL;
			}
		}

		if (config_lookup_bool(config, "application.tunnelmode.localbridging", &configBool) == CONFIG_TRUE) {
			if (configBool != 0) {
				g_wtp.mactunnel.mode |=  CAPWAP_WTP_LOCAL_BRIDGING;
			}
		}
	}

	/* Set mactype of WTP */
	if (config_lookup_string(config, "application.mactype", &configString) == CONFIG_TRUE) {
		if (!strcmp(configString, "localmac")) {
			g_wtp.mactype.type = CAPWAP_LOCALMAC;
		} else if (!strcmp(configString, "splitmac")) {
			g_wtp.mactype.type = CAPWAP_SPLITMAC;
		} else {
			log_printf(LOG_ERR, "Invalid configuration file, unknown application.mactype value");
			return 0;
		}
	}

	/* Set VendorID Boardinfo of WTP */
	if (config_lookup_int(config, "application.boardinfo.idvendor", &configInt) == CONFIG_TRUE) {
		g_wtp.boarddata.vendor = (unsigned long)configInt;
	}

	if (wtp_parsing_cfg_boardinfo_element(config) != 1)
		return 0;


	/* Set WLAN WTP */
	if (config_lookup_string(config, "wlan.prefix", &configString) == CONFIG_TRUE) {
		int length = strlen(configString);

		if ((length > 0) && (length < WTP_PREFIX_NAME_MAX_LENGTH)) {
			strcpy(g_wtp.wlanprefix, configString);
		} else {
			log_printf(LOG_ERR, "Invalid configuration file, wlan.prefix string length exceeded");
			return 0;
		}
	}

	/* Set Radio WTP */
	configSetting = config_lookup(config, "application.radio");
	if (configSetting)
		if (wtp_parsing_radio_section_configuration(configSetting) == 0)
			return 0;

	/* Set encryption of WTP */
	configSetting = config_lookup(config, "application.descriptor.encryption");
	if (configSetting != NULL) {
		unsigned short capability = 0;
		int count = config_setting_length(configSetting);
		struct capwap_wtpdescriptor_encrypt_subelement* encrypt;
		
		if (g_wtp.binding == CAPWAP_WIRELESS_BINDING_IEEE80211) {
			for (i = 0; i < count; i++) {
				const char* encryption = config_setting_get_string_elem(configSetting, i);
				if (encryption != NULL) {
					if (!strcmp(encryption, "802.11_AES")) {
						capability |= 0; /* TODO */
					} else if (!strcmp(encryption, "802.11_TKIP")) {
						capability |= 0; /* TODO */
					} else {
						log_printf(LOG_ERR, "Invalid configuration file, invalid application.descriptor.encryption value");
						return 0;
					}
				}
			}
		}

		/* */
		encrypt = (struct capwap_wtpdescriptor_encrypt_subelement*)capwap_array_get_item_pointer(g_wtp.descriptor.encryptsubelement, g_wtp.descriptor.encryptsubelement->count);
		encrypt->wbid = g_wtp.binding;
		encrypt->capabilities = capability;
	} else {
		log_printf(LOG_ERR, "Invalid configuration file, application.descriptor.encryption not found");
		return 0;
	}

	/* Set info descriptor of WTP */
	if (wtp_parsing_cfg_descriptor_info(config) != 1)
		return 0;

	/* Set ECN of WTP */
	if (config_lookup_string(config, "application.ecn", &configString) == CONFIG_TRUE) {
		if (!strcmp(configString, "full")) {
			g_wtp.ecn.flag = CAPWAP_FULL_ECN_SUPPORT;
		} else if (!strcmp(configString, "limited")) {
			g_wtp.ecn.flag = CAPWAP_LIMITED_ECN_SUPPORT;
		} else {
			log_printf(LOG_ERR, "Invalid configuration file, unknown application.ecn value");
			return 0;
		}
	}

	/* Set Timer of WTP */
	if (config_lookup_int(config, "application.timer.statistics", &configInt) == CONFIG_TRUE) {
		if ((configInt > 0) && (configInt < 65536)) {
			g_wtp.statisticstimer.timer = (unsigned short)configInt;
		} else {
			log_printf(LOG_ERR, "Invalid configuration file, invalid application.timer.statistics value");
			return 0;
		}
	}

	/* Set DTLS of WTP */
	if (config_lookup_bool(config, "application.dtls.enable", &configBool) == CONFIG_TRUE) {
		if (configBool != 0) {
			struct capwap_dtls_param dtlsparam;

			/* Init dtls param */
			memset(&dtlsparam, 0, sizeof(struct capwap_dtls_param));
			dtlsparam.type = CAPWAP_DTLS_CLIENT;

			/* Set DTLS Policy of WTP */
			if (config_lookup(config, "application.dtls.dtlspolicy") != NULL) {
				g_wtp.validdtlsdatapolicy = 0;
				if (config_lookup_bool(config, "application.dtls.dtlspolicy.cleardatachannel", &configBool) == CONFIG_TRUE) {
					if (configBool != 0) {
						g_wtp.validdtlsdatapolicy |= CAPWAP_ACDESC_CLEAR_DATA_CHANNEL_ENABLED;
					}
				}
		
				if (config_lookup_bool(config, "application.dtls.dtlspolicy.dtlsdatachannel", &configBool) == CONFIG_TRUE) {
					if (configBool != 0) {
						g_wtp.validdtlsdatapolicy |= CAPWAP_ACDESC_DTLS_DATA_CHANNEL_ENABLED;
					}
				}
			}

			/* Set DTLS type of WTP */
			if (config_lookup_string(config, "application.dtls.type", &configString) == CONFIG_TRUE) {
				if (!strcmp(configString, "x509")) {
					dtlsparam.mode = CAPWAP_DTLS_MODE_CERTIFICATE;
				} else if (!strcmp(configString, "presharedkey")) {
					dtlsparam.mode = CAPWAP_DTLS_MODE_PRESHAREDKEY;
				} else {
					log_printf(LOG_ERR, "Invalid configuration file, unknown application.dtls.type value");
					return 0;
				}
			}

			/* Set DTLS configuration of WTP */
			if (dtlsparam.mode == CAPWAP_DTLS_MODE_CERTIFICATE) {
				if (config_lookup_string(config, "application.dtls.x509.calist", &configString) == CONFIG_TRUE) {
					if (strlen(configString) > 0) {
						dtlsparam.cert.fileca = capwap_duplicate_string(configString);
					}
				}

				if (config_lookup_string(config, "application.dtls.x509.certificate", &configString) == CONFIG_TRUE) {
					if (strlen(configString) > 0) {
						dtlsparam.cert.filecert = capwap_duplicate_string(configString);
					}
				}

				if (config_lookup_string(config, "application.dtls.x509.privatekey", &configString) == CONFIG_TRUE) {
					if (strlen(configString) > 0) {
						dtlsparam.cert.filekey = capwap_duplicate_string(configString);
					}
				}

				/* */
				if (dtlsparam.cert.fileca && dtlsparam.cert.filecert && dtlsparam.cert.filekey) {
					if (capwap_crypt_createcontext(&g_wtp.dtlscontext, &dtlsparam)) {
						g_wtp.enabledtls = 1;
					}
				}

				/* Free dtls param */
				if (dtlsparam.cert.fileca) {
					capwap_free(dtlsparam.cert.fileca);
				}

				if (dtlsparam.cert.filecert) {
					capwap_free(dtlsparam.cert.filecert);
				}

				if (dtlsparam.cert.filekey) {
					capwap_free(dtlsparam.cert.filekey);
				}
			} else if (dtlsparam.mode == CAPWAP_DTLS_MODE_PRESHAREDKEY) {
				if (config_lookup_string(config, "application.dtls.presharedkey.identity", &configString) == CONFIG_TRUE) {
					if (strlen(configString) > 0) {
						dtlsparam.presharedkey.identity = capwap_duplicate_string(configString);
					}
				}

				if (config_lookup_string(config, "application.dtls.presharedkey.pskkey", &configString) == CONFIG_TRUE) {
					if (strlen(configString) > 0) {
						dtlsparam.presharedkey.pskkey = capwap_duplicate_string(configString);
					}
				}

				/* */
				if (dtlsparam.presharedkey.identity && dtlsparam.presharedkey.pskkey) {
					if (capwap_crypt_createcontext(&g_wtp.dtlscontext, &dtlsparam)) {
						g_wtp.enabledtls = 1;
					}
				}

				/* Free dtls param */
				if (dtlsparam.presharedkey.identity) {
					capwap_free(dtlsparam.presharedkey.identity);
				}

				if (dtlsparam.presharedkey.pskkey) {
					capwap_free(dtlsparam.presharedkey.pskkey);
				}
			}

			if (!g_wtp.enabledtls) {
				return 0;
			}
		}
	}

	/* Set interface binding of WTP */
	if (config_lookup_string(config, "application.network.binding", &configString) == CONFIG_TRUE) {
		if (strlen(configString) > (IFNAMSIZ - 1)) {
			log_printf(LOG_ERR, "Invalid configuration file, application.network.binding string length exceeded");
			return 0;
		}			
			
		strcpy(g_wtp.net.bindiface, configString);
	}

	/* Set mtu of WTP */
	if (config_lookup_int(config, "application.network.mtu", &configInt) == CONFIG_TRUE) {
		if ((configInt > 0) && (configInt < 65536)) {
			g_wtp.mtu = (unsigned short)configInt;
		} else {
			log_printf(LOG_ERR, "Invalid configuration file, invalid application.network.mtu value");
			return 0;
		}
	}

	/* Set transport of WTP */
	if (config_lookup_string(config, "application.network.transport", &configString) == CONFIG_TRUE) {
		if (!strcmp(configString, "udp")) {
			g_wtp.transport.type = CAPWAP_UDP_TRANSPORT;
		} else if (!strcmp(configString, "udplite")) {
			g_wtp.transport.type = CAPWAP_UDPLITE_TRANSPORT;
		} else {
			log_printf(LOG_ERR, "Invalid configuration file, unknown application.network.transport value");
			return 0;
		}
	}

	/* Set search discovery of WTP */
	if (config_lookup_bool(config, "application.acdiscovery.search", &configBool) == CONFIG_TRUE) {
		g_wtp.acdiscoveryrequest = (configBool ? 1 : 0);
	}

	/* Set discovery host of WTP */
	configSetting = config_lookup(config, "application.acdiscovery.host");
	if (configSetting != NULL) {
		int count = config_setting_length(configSetting);

		for (i = 0; i < count; i++) {
			const char* address = config_setting_get_string_elem(configSetting, i);
			if (address != NULL) {
				struct addr_capwap acaddr;
				memset(&acaddr, 0, sizeof(struct addr_capwap));
				strncpy(acaddr.fqdn, address, CAPWAP_MAX_FQDN_SIZE-1);
				acaddr.resolved = 0;

				/* Parsing address */
				if (capwap_address_from_string(address, &acaddr.sockaddr)) {
					if (!CAPWAP_GET_NETWORK_PORT(&acaddr.sockaddr)) {
						CAPWAP_SET_NETWORK_PORT(&acaddr.sockaddr, CAPWAP_CONTROL_PORT);
					}
					acaddr.resolved = 1;
					g_wtp.discoverytype.type = CAPWAP_DISCOVERYTYPE_TYPE_STATIC;
				} else {
					log_printf(LOG_INFO, "%s:%d Could not resolve application.acdiscovery.host %s", __FILE__, __LINE__, address);
				}
				memcpy(capwap_array_get_item_pointer(g_wtp.acdiscoveryarray, g_wtp.acdiscoveryarray->count), &acaddr, sizeof(struct addr_capwap));
			}
		}
	}

	/* Set preferred ac of WTP */
	configSetting = config_lookup(config, "application.acprefered.host");
	if (configSetting != NULL) {
		int count = config_setting_length(configSetting);

		for (i = 0; i < count; i++) {
			const char* address = config_setting_get_string_elem(configSetting, i);
			if (address != NULL) {
				struct addr_capwap acaddr;
				memset(&acaddr, 0, sizeof(struct addr_capwap));
				strncpy(acaddr.fqdn, address, CAPWAP_MAX_FQDN_SIZE-1);
				acaddr.resolved = 0;

				/* Parsing address */
				if (capwap_address_from_string(address, &acaddr.sockaddr)) {
					if (!CAPWAP_GET_NETWORK_PORT(&acaddr.sockaddr)) {
						CAPWAP_SET_NETWORK_PORT(&acaddr.sockaddr, CAPWAP_CONTROL_PORT);
					}
					acaddr.resolved = 1;
				} else {
					log_printf(LOG_INFO, "%s:%d Could not resolve application.acprefered.host %s", __FILE__, __LINE__, acaddr.fqdn);
				}
				memcpy(capwap_array_get_item_pointer(g_wtp.acpreferedarray, g_wtp.acpreferedarray->count), &acaddr, sizeof(struct addr_capwap));
			}
		}
	}

	return 1;
}

/* Parsing configuration */
static int wtp_parsing_configuration(config_t* config) {
	const char* configString;
	
	if (config_lookup_string(config, "version", &configString) == CONFIG_TRUE) {
		if (strcmp(configString, "1.0") == 0) {
			return wtp_parsing_configuration_1_0(config);
		}
		
		log_printf(LOG_ERR, "Invalid configuration file, '%s' is not supported", configString);
	} else {
		log_printf(LOG_ERR, "Invalid configuration file, unable to found version tag");
	}

	return 0;
}

/* Load configuration */
static int wtp_load_configuration(int argc, char **argv) {
	int c;
	int result = 0;
	config_t config;
	
	ASSERT(argc >= 0);
	ASSERT(argv != NULL);
	
	/* Parsing command line */
	opterr = 0;
	while ((c = getopt(argc, argv, "hc:")) != -1) {
		switch (c) {
			case 'h': {
				wtp_print_usage();
				return 0;
			}
						
			case 'c': {
				if (strlen(optarg) < sizeof(g_configurationfile)) {
					strcpy(g_configurationfile, optarg);
				} else {
					log_printf(LOG_ERR, "Invalid -%c argument", optopt);
					return -1;
				}
				
				break;
			}
			
			case '?': {
				if (optopt == 'c') {
					log_printf(LOG_ERR, "Option -%c requires an argument", optopt);
				} else {
					log_printf(LOG_ERR, "Unknown option character `\\x%x'", optopt);
				}
				
				wtp_print_usage();
				return -1;
			}
		}
	}

	/* Init libconfig */
	config_init(&config);

	/* Load configuration */
	if (config_read_file(&config, g_configurationfile) == CONFIG_TRUE) {
		result = wtp_parsing_configuration(&config);
	} else {
		result = -1;
		log_printf(LOG_ERR, "Unable load the configuration file '%s': %s (%d)", g_configurationfile, config_error_text(&config), config_error_line(&config));
	}

	/* Free libconfig */
	config_destroy(&config);
	return result;
}

/* Init WTP */
static int wtp_configure(void) {
	/* If not set try IPv6 */
	if (g_wtp.net.localaddr.ss.ss_family == AF_UNSPEC) {
		g_wtp.net.localaddr.ss.ss_family = AF_INET6;
	}

	/* If request add default acdiscovery */
	if (!g_wtp.acdiscoveryarray->count) {
		wtp_add_default_acaddress();
	}

	/* Bind control address */
	if (capwap_bind_sockets(&g_wtp.net)) {
		log_printf(LOG_EMERG, "Cannot bind control address");
		return WTP_ERROR_NETWORK;
	}
	wtp_socket_io_start();

	return CAPWAP_SUCCESSFUL;
}

static void wtp_wait_radio_ready_timeout_cb(EV_P_ ev_timer *w, int revents)
{
	ev_break (EV_A_ EVBREAK_ONE);
}

/* */
static void wtp_wait_radio_ready(void)
{
	ev_timer timeout;

	ev_timer_init(&timeout, wtp_wait_radio_ready_timeout_cb,
		      WTP_RADIO_INITIALIZATION_INTERVAL / 1000, 0.);
	ev_timer_start(EV_DEFAULT_UC_ &timeout);

	ev_run(EV_DEFAULT_UC_ 0);
}

/* */
int wtp_update_radio_in_use() {
	/* TODO */
	return g_wtp.radios->count;
}

/* Main*/
int main(int argc, char** argv) {
	int value;
	int result = CAPWAP_SUCCESSFUL;

	ev_default_loop(0);

	/* Init logging */
	capwap_logging_init();
	capwap_logging_verboselevel(LOG_ERR);
	capwap_logging_enable_console(1);

	/* Init capwap */
	if (geteuid() != 0) {
		log_printf(LOG_EMERG, "Request root privileges");
		result = CAPWAP_REQUEST_ROOT;

		goto out_close_log;
	}

	/* Init random generator */
	capwap_init_rand();

	/* Init crypt */
	if (capwap_crypt_init()) {
		result = CAPWAP_CRYPT_ERROR;
		log_printf(LOG_EMERG, "Error to init crypt engine");

		goto out_check_memory;
	}

	/* Init WTP */
	if (!wtp_init()) {
		result = WTP_ERROR_SYSTEM_FAILER;
		log_printf(LOG_EMERG, "Error to init WTP engine");

		goto out_release_crypto;
	}

	/* Read configuration file */
	value = wtp_load_configuration(argc, argv);
	if (value < 0) {
		result = WTP_ERROR_LOAD_CONFIGURATION;
		log_printf(LOG_EMERG, "Error to load configuration");

		goto out_destroy_wtp;
	} else if (value == 0)
		/* error already reported in config parser */
		goto out_destroy_wtp;

	if (!g_wtp.standalone) {
		capwap_daemon();

		/* Console logging is disabled in daemon mode */
		capwap_logging_disable_console();
		log_printf(LOG_INFO, "Running WTP in daemon mode");
	}

	/* Wait the initialization of radio interfaces */
	log_printf(LOG_INFO, "Wait for the initialization of radio interfaces");
	wtp_wait_radio_ready();

	/* Connect WTP with kernel module */
	if (wtp_kmod_init()) {
		log_printf(LOG_EMERG, "Unable to connect with kernel module");
		goto out_close_radio;
	}

	log_printf(LOG_INFO, "SmartCAPWAP kernel module connected");

	/* */
	log_printf(LOG_INFO, "Startup WTP");

	/* Complete configuration WTP */
	result = wtp_configure();
	if (result == CAPWAP_SUCCESSFUL) {
		/* Running WTP */

		result = wtp_dfa_running();

		log_printf(LOG_INFO, "wtp_dfa_running returned with result %d", result);

		/* Close sockets */
		wtp_socket_io_stop();
		capwap_close_sockets(&g_wtp.net);
	}

	/* Disconnect kernel module */
	wtp_kmod_free();

	/* */
	log_printf(LOG_INFO, "Terminate WTP");

out_close_radio:
	/* Close radio */
	wtp_radio_close();

	/* Free binding */
	if (g_wtp.binding == CAPWAP_WIRELESS_BINDING_IEEE80211) {
		log_printf(LOG_INFO, "Free wifi binding engine");
		wifi_driver_free();
	}

out_destroy_wtp:
	/* Free memory */
	wtp_destroy();

out_release_crypto:
	/* Free crypt */
	capwap_crypt_free();

out_check_memory:
	/* Check memory leak */
	if (capwap_check_memory_leak(1)) {
		if (result == CAPWAP_SUCCESSFUL)
			result = WTP_ERROR_MEMORY_LEAK;
	}

out_close_log:
	/* Close logging */
	capwap_logging_close();

	return result;
}
