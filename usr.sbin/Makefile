#	$OpenBSD: Makefile,v 1.171 2014/01/05 16:03:48 jsing Exp $

.include <bsd.own.mk>

SUBDIR=	ac accton acpidump adduser apm apmd arp \
	authpf bgpctl bgpd bind chroot config cron crunchgen dev_mkdb \
	dhcpd dhcrelay dvmrpctl dvmrpd edquota fdformat \
	ftp-proxy fw_update gpioctl hostapd hotplugd identd ifstated \
	ikectl inetd installboot iostat iscsictl iscsid kgmon kvm_mkdb \
	ldapd ldapctl ldpd ldpctl lpr mailwrapper map-mbone \
	memconfig mkuboot mrinfo mrouted \
	mtrace mtree ndp netgroup_mkdb \
	nginx npppctl npppd nsd ntpd openssl ospfctl ospfd ospf6d ospf6ctl \
	pcidump pkg_add portmap ppp pppd pppoe procmap pstat pwd_mkdb \
	quot quotaon rarpd rbootd rdate relayctl relayd repquota rip6query \
	ripctl ripd rmt route6d rpc.bootparamd rpc.lockd rpc.statd rtadvd \
	rtsold rwhod sa sasyncd sensorsd sliplogin slowcgi slstats smtpd \
	snmpctl snmpd spamdb spray syslogc syslogd sysmerge tcpdrop tcpdump \
	tftp-proxy tftpd tokenadm tokeninit traceroute traceroute6 trpt \
	usbdevs user vipw watchdogd wsconscfg wsfontload wsmoused zdump zic

.if (${YP:L} == "yes")
SUBDIR+=ypbind ypldap yppoll ypset ypserv
.endif

.include <bsd.subdir.mk>

