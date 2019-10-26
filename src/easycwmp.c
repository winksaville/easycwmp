/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Copyright (C) 2012-2014 PIVA SOFTWARE (www.pivasoftware.com)
 *		Author: Mohamed Kallel <mohamed.kallel@pivasoftware.com>
 *		Author: Anis Ellouze <anis.ellouze@pivasoftware.com>
 *	Copyright (C) 2011-2012 Luka Perkov <freecwmp@lukaperkov.net>
 */

#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <getopt.h>
#include <limits.h>
#include <locale.h>
#include <unistd.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <libubox/uloop.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/file.h>

#include "json.h"
#include "easycwmp.h"
#include "config.h"
#include "cwmp.h"
#include "ubus.h"
#include "log.h"
#include "external.h"
#include "backup.h"
#include "http.h"
#include "xml.h"

static struct easycwmp_ctx ctx = {
	.install_dir = NULL,
	.pid_file = NULL,
	.ubus_socket_file = NULL,
};
struct easycwmp_ctx *ctx_easycwmp = &ctx;

static void easycwmp_do_reload(struct uloop_timeout *timeout);
static void easycwmp_do_notify(struct uloop_timeout *timeout);
static void netlink_new_msg(struct uloop_fd *ufd, unsigned events);

static struct uloop_fd netlink_event = { .cb = netlink_new_msg };
static struct uloop_timeout reload_timer = { .cb = easycwmp_do_reload };
static struct uloop_timeout notify_timer = { .cb = easycwmp_do_notify };

static struct option long_opts[] = {
	{"foreground", no_argument, NULL, 'f'},
	{"help", no_argument, NULL, 'h'},
	{"version", no_argument, NULL, 'v'},
	{"boot", no_argument, NULL, 'b'},
	{"getrpcmethod", no_argument, NULL, 'g'},
	{"installdir", required_argument, NULL, 'i'},
	{"ubussockfile", required_argument, NULL, 's'},
	{NULL, 0, NULL, 0}
};

static void print_help(void)
{
	printf("Usage: %s [OPTIONS]\n", NAME);
	printf(" -f, --foreground        Run in the foreground\n");
	printf(" -b, --boot              Run with \"1 BOOT\" event\n");
	printf(" -g, --getrpcmethod      Run with \"2 PERIODIC\" event and with ACS GetRPCMethods\n");
	printf(" -h, --help              Display this help text\n");
	printf(" -i, --installdir        installation directory or export EASYCWMP_INSTALL_DIR\n");
	printf(" -s, --ubussockfile      full path to ubus socket file\n");
	printf(" -v, --version           Display the %s version\n", NAME);
}

static void print_version(void)
{
	printf("%s version: %s\n", NAME, EASYCWMP_VERSION);
}

static void easycwmp_do_reload(struct uloop_timeout *timeout)
{
	log_message(NAME, L_NOTICE, "configuration reload\n");
	if (external_init()) {
		D("external scripts initialization failed\n");
		return;
	}
	config_load();
	external_exit();
}

static void easycwmp_do_notify(struct uloop_timeout *timeout)
{
	log_message(NAME, L_NOTICE, "checking if there is notify value change\n");
	if (external_init()) {
		D("external scripts initialization failed\n");
		return;
	}
	external_action_simple_execute("check_value_change", NULL, NULL);
	external_action_handle(json_handle_check_parameter_value_change);
	external_exit();
}

void easycwmp_reload(void)
{
	uloop_timeout_set(&reload_timer, 100);
}

void easycwmp_notify(void)
{
	uloop_timeout_set(&notify_timer, 1);
}


static void easycwmp_netlink_interface(struct nlmsghdr *nlh)
{
	struct ifaddrmsg *ifa = (struct ifaddrmsg *) NLMSG_DATA(nlh);
	struct rtattr *rth = IFA_RTA(ifa);
	int rtl = IFA_PAYLOAD(nlh);
	char if_name[IFNAMSIZ], if_addr[INET_ADDRSTRLEN];
	static uint32_t old_addr=0;

	memset(&if_name, 0, sizeof(if_name));
	memset(&if_addr, 0, sizeof(if_addr));

	while (rtl && RTA_OK(rth, rtl)) {
		if (rth->rta_type != IFA_LOCAL) {
			rth = RTA_NEXT(rth, rtl);
			continue;
		}

		uint32_t addr = htonl(* (uint32_t *)RTA_DATA(rth));
		if (htonl(13) == 13) {
			// running on big endian system
		} else {
			// running on little endian system
			addr = __builtin_bswap32(addr);
		}

		if_indextoname(ifa->ifa_index, if_name);
		if (strncmp(config->local->interface, if_name, IFNAMSIZ)) {
			rth = RTA_NEXT(rth, rtl);
			continue;
		}

		if ((addr != old_addr) && (old_addr != 0)) {
			log_message(NAME, L_NOTICE, "ip address of the interface %s is changed\n",	if_name);
			cwmp_add_event(EVENT_VALUE_CHANGE, NULL, 0, EVENT_NO_BACKUP);
			cwmp_add_inform_timer();
		}
		old_addr = addr;

		inet_ntop(AF_INET, &(addr), if_addr, INET_ADDRSTRLEN);

		if (config->local) FREE(config->local->ip);
		config->local->ip = strdup(if_addr);
		break;
	}

	if (strlen(if_addr) == 0) return;

	log_message(NAME, L_NOTICE, "interface %s has ip %s\n",
			if_name, if_addr);
}

static void
netlink_new_msg(struct uloop_fd *ufd, unsigned events)
{
	struct nlmsghdr *nlh;
	char buffer[BUFSIZ];
	int msg_size;

	memset(&buffer, 0, sizeof(buffer));

	nlh = (struct nlmsghdr *)buffer;
	if ((msg_size = recv(ufd->fd, nlh, BUFSIZ, 0)) == -1) {
		DD("error receiving netlink message");
		return;
	}

	while (msg_size > sizeof(*nlh)) {
		int len = nlh->nlmsg_len;
		int req_len = len - sizeof(*nlh);

		if (req_len < 0 || len > msg_size) {
			DD("error reading netlink message");
			return;
		}

		if (!NLMSG_OK(nlh, msg_size)) {
			DD("netlink message is not NLMSG_OK");
			return;
		}

		if (nlh->nlmsg_type == RTM_NEWADDR)
			easycwmp_netlink_interface(nlh);

		msg_size -= NLMSG_ALIGN(len);
		nlh = (struct nlmsghdr*)((char*)nlh + NLMSG_ALIGN(len));
	}
}

static int netlink_init(void)
{
	struct {
		struct nlmsghdr hdr;
		struct ifaddrmsg msg;
	} req;
	struct sockaddr_nl addr;

	memset(&addr, 0, sizeof(addr));
	memset(&req, 0, sizeof(req));

	if ((cwmp->netlink_sock[0] = socket(PF_NETLINK, SOCK_RAW, NETLINK_ROUTE)) == -1) {
		D("couldn't open NETLINK_ROUTE socket");
		return -1;
	}
	if (fcntl(cwmp->netlink_sock[0], F_SETFD, fcntl(cwmp->netlink_sock[0], F_GETFD) | FD_CLOEXEC) < 0)
		log_message(NAME, L_NOTICE, "error in fcntl\n");

	addr.nl_family = AF_NETLINK;
	addr.nl_groups = RTMGRP_IPV4_IFADDR;
	if ((bind(cwmp->netlink_sock[0], (struct sockaddr *)&addr, sizeof(addr))) == -1) {
		D("couldn't bind netlink socket");
		return -1;
	}

	netlink_event.fd = cwmp->netlink_sock[0];
	uloop_fd_add(&netlink_event, ULOOP_READ | ULOOP_EDGE_TRIGGER);

	if ((cwmp->netlink_sock[1] = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE)) == -1) {
		D("couldn't open NETLINK_ROUTE socket");
		return -1;
	}
	if (fcntl(cwmp->netlink_sock[1], F_SETFD, fcntl(cwmp->netlink_sock[1], F_GETFD) | FD_CLOEXEC) < 0)
		log_message(NAME, L_NOTICE, "error in fcntl\n");

	req.hdr.nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
	req.hdr.nlmsg_flags = NLM_F_REQUEST | NLM_F_ROOT;
	req.hdr.nlmsg_type = RTM_GETADDR;
	req.msg.ifa_family = AF_INET;

	if ((send(cwmp->netlink_sock[1], &req, req.hdr.nlmsg_len, 0)) == -1) {
		D("couldn't send netlink socket");
		return -1;
	}

	struct uloop_fd dummy_event = { .fd = cwmp->netlink_sock[1] };
	netlink_new_msg(&dummy_event, 0);

	return 0;
}

int main (int argc, char **argv)
{
	D("+\n");
	D("env UCI_CONFIG_DIR=%s\n", getenv("UCI_CONFIG_DIR"));
	D("env EASYCWMP_INSTALL_DIR=%s\n", getenv("EASYCWMP_INSTALL_DIR"));
	int c;
	int start_event = 0;
	bool foreground = false;

	D("Get command line options\n");
	while (1) {
		D("Get command line options TOP\n");
		c = getopt_long(argc, argv, "fhbgvi:s:", long_opts, NULL);
		D("%d:%c optind=%d opterr=%d optopt=%d optarg=%s\n", c, c, optind, opterr, optopt, optarg);
		if (c == EOF) {
			D("EOF\n");
			break;
		}
		switch (c) {
			case 'b':
				start_event |= START_BOOT;
				break;
			case 'f':
				foreground = true;
				break;
			case 'g':
				start_event |= START_GET_RPC_METHOD;
				break;
			case 'h':
				print_help();
				exit(EXIT_SUCCESS);
			case 'v':
				print_version();
				exit(EXIT_SUCCESS);
			case 'i':
				D("i optind=%d optarg=%s", optind, optarg);
				ctx_easycwmp->install_dir = strdup(optarg);
				break;
			case 's':
				D("s optind=%d optarg=%s", optind, optarg);
				ctx_easycwmp->ubus_socket_file = strdup(optarg);
				break;
			default:
				print_help();
				exit(EXIT_FAILURE);
		}
	}

	if (NULL == ctx_easycwmp->install_dir) {
		ctx_easycwmp->install_dir = getenv("EASYCWMP_INSTALL_DIR");
	}
	if ((NULL == ctx_easycwmp->install_dir) || (0 == strlen(ctx_easycwmp->install_dir))) {
		ctx_easycwmp->install_dir = strdup(EASYCWMP_INSTALL_DIR_DEFAULT);
		D("No -i option or EASYCWMP_INSTALL_DIR environment variable, assuming '%s'\n", ctx_easycwmp->install_dir);
	}
	if (ctx_easycwmp->install_dir[strlen(ctx_easycwmp->install_dir)-1] != '/') {
		// Be sure Append '/'
		char *tmp;
		if (asprintf(&tmp, "%s/", ctx_easycwmp->install_dir) < 0) {
			log_message(NAME, L_CRIT, "Err: Could not append '/' to ctx_easycwmp->install_dir string errno=%d:%s\n", errno, strerror(errno));
			exit(EXIT_FAILURE);
		}
		ctx_easycwmp->install_dir = tmp;
	}
	D("ctx_easycwmp->install_dir=%s\n", ctx_easycwmp->install_dir);

	if (asprintf(&ctx_easycwmp->pid_file, "%s%s", ctx_easycwmp->install_dir, "var/run/easycwmp.pid") < 0) {
		log_message(NAME, L_CRIT, "Err: Could not create pid_file errno=%d:%s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	D("ctx_easycwmp->pid_file=%s\n", ctx_easycwmp->pid_file);
	int fd = open(ctx_easycwmp->pid_file, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if(fd == -1) {
		D("Err: Could not open pid_file '%s' errno=%d:%s\n", ctx_easycwmp->pid_file, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (flock(fd, LOCK_EX | LOCK_NB) == -1)
		exit(EXIT_SUCCESS);
	if(fcntl(fd, F_SETFD, fcntl(fd, F_GETFD) | FD_CLOEXEC) < 0 )
		log_message(NAME, L_NOTICE, "error in fcntl\n");

	setlocale(LC_CTYPE, "");
	umask(0037);

	D("init stuff\n");
	/* run early cwmp initialization */
	cwmp = calloc(1, sizeof(struct cwmp_internal));
	if (!cwmp) return -1;

	INIT_LIST_HEAD(&cwmp->events);
	INIT_LIST_HEAD(&cwmp->notifications);
	INIT_LIST_HEAD(&cwmp->downloads);
	INIT_LIST_HEAD(&cwmp->uploads);
	INIT_LIST_HEAD(&cwmp->scheduled_informs);
	uloop_init();
	backup_init();
	if (external_init()) {
		D("external scripts initialization failed\n");
		return -1;
	}
	D("call config_load\n");
	config_load();
	D("call config_init_deviceid\n");
	cwmp_init_deviceid();

	D("call external_exit\n");
	external_exit();

	if (start_event & START_BOOT) {
		D("call external_exit\n");
		cwmp_add_event(EVENT_BOOT, NULL, 0, EVENT_BACKUP);
		cwmp_add_inform_timer();
	}
	if (start_event & START_GET_RPC_METHOD) {
		cwmp->get_rpc_methods = true;
		cwmp_add_event(EVENT_PERIODIC, NULL, 0, EVENT_BACKUP);
		cwmp_add_inform_timer();
	}

	if (netlink_init()) {
		D("netlink initialization failed\n");
		exit(EXIT_FAILURE);
	}

	if (ubus_init()) D("ubus initialization failed\n");

	http_server_init();

	pid_t pid, sid;
	if (!foreground) {
		pid = fork();
		if (pid < 0)
			exit(EXIT_FAILURE);
		if (pid > 0)
			exit(EXIT_SUCCESS);

		sid = setsid();
		if (sid < 0) {
			D("setsid() returned error\n");
			exit(EXIT_FAILURE);
		}

		char *directory = "/";

		if ((chdir(directory)) < 0) {
			D("chdir() returned error\n");
			exit(EXIT_FAILURE);
		}
	}
	log_message(NAME, L_NOTICE, "daemon started\n");

	D("Initalization complete write pid\n");
	char *buf = NULL;
	if (asprintf(&buf, "%d", getpid()) < 0) {
		log_message(NAME, L_CRIT, "Err: Unable to convert pid to string, errno=%d:%s\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	if (write(fd, buf, strlen(buf) < 0)) {
		log_message(NAME, L_CRIT, "Err: Unable to write pid to '%s', errno=%d:%s\n", ctx_easycwmp->pid_file, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
	free(buf);

	log_message(NAME, L_NOTICE, "entering main loop\n");
	uloop_run();

	D("main loop done, cleaning up\n");
	ubus_exit();
	uloop_done();

	http_client_exit();
	xml_exit();
	config_exit();
	cwmp_free_deviceid();

	closelog();
	close(fd);
	if (cwmp->netlink_sock[0] != -1) close(cwmp->netlink_sock[0]);
	if (cwmp->netlink_sock[1] != -1) close(cwmp->netlink_sock[1]);
	free(cwmp);
	FREE(ctx_easycwmp->install_dir);
	FREE(ctx_easycwmp->pid_file);
	FREE(ctx_easycwmp->ubus_socket_file);

	log_message(NAME, L_NOTICE, "exiting\n");
	D("-\n");
	return 0;
}

