# SmartCAPWAP -- An Open Source CAPWAP WTP / AC
#
# Copyright (C) 2012-2013 Massimo Vellucci <vemax78@gmail.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program (see the file COPYING included with this
# distribution); if not, write to the Free Software Foundation, Inc.,
# 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

MAINTAINERCLEANFILES = $(srcdir)/Makefile.in

bin_PROGRAMS = ac

AM_CFLAGS = -DCAPWAP_MULTITHREADING_ENABLE \
	-D_REENTRANT \
	-D_GNU_SOURCE \
	${LIBNL_CFLAGS} \
	$(LIBXML2_CFLAGS) \
	$(WOLFSSL_CFLAGS)

AM_CFLAGS += -I$(top_srcdir)/build \
	-I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/ac \
	-I$(top_srcdir)/src/ac/kmod \
	-I$(top_srcdir)/src/common/binding/ieee80211 

include $(top_srcdir)/build/Makefile_common.am

ac_SOURCES = $(capwap_SOURCES) \
	../../src/common/capwap_event.c \
	../../src/common/capwap_lock.c \
	../../src/common/capwap_rwlock.c \
	../../src/common/capwap_socket.c \
	../../src/ac/ac.c \
	../../src/ac/ac_backend.c \
	../../src/ac/ac_execute.c \
	../../src/ac/ac_session.c \
	../../src/ac/ac_wlans.c \
	../../src/ac/ac_kmod.c \
	../../src/ac/ac_ieee80211_data.c \
	../../src/ac/ac_discovery.c \
	../../src/ac/ac_80211_json.c \
	../../src/ac/ac_80211_json_addwlan.c \
	../../src/ac/ac_80211_json_antenna.c \
	../../src/ac/ac_80211_json_assignbssid.c \
	../../src/ac/ac_80211_json_deletewlan.c \
	../../src/ac/ac_80211_json_directsequencecontrol.c \
	../../src/ac/ac_80211_json_ie.c \
	../../src/ac/ac_80211_json_macoperation.c \
	../../src/ac/ac_80211_json_miccountermeasures.c \
	../../src/ac/ac_80211_json_multidomaincapability.c \
	../../src/ac/ac_80211_json_ofdmcontrol.c \
	../../src/ac/ac_80211_json_rateset.c \
	../../src/ac/ac_80211_json_rsnaerrorreport.c \
	../../src/ac/ac_80211_json_statistics.c \
	../../src/ac/ac_80211_json_supportedrates.c \
	../../src/ac/ac_80211_json_txpower.c \
	../../src/ac/ac_80211_json_txpowerlevel.c \
	../../src/ac/ac_80211_json_updatewlan.c \
	../../src/ac/ac_80211_json_wtpqos.c \
	../../src/ac/ac_80211_json_wtpradioconf.c \
	../../src/ac/ac_80211_json_wtpradiofailalarm.c \
	../../src/ac/ac_80211_json_wtpradioinformation.c \
	../../src/ac/ac_dfa_join.c \
	../../src/ac/ac_dfa_configure.c \
	../../src/ac/ac_dfa_imagedata.c \
	../../src/ac/ac_dfa_datacheck.c \
	../../src/ac/ac_dfa_dtls.c \
	../../src/ac/ac_dfa_run.c \
	../../src/ac/ac_dfa_reset.c \
	../../src/ac/ac_dfa_teardown.c \
	../../src/ac/ac_soap.c \
	../../src/common/binding/ieee80211/ieee80211.c

ac_LDADD = $(CONFIG_LIBS) \
	$(PTHREAD_LIBS) \
	$(LIBXML2_LIBS) \
	$(LIBJSON_LIBS) \
	$(WOLFSSL_LIBS) \
	$(LIBNL_LIBS)
