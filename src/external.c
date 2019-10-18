/*
 *	This program is free software: you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation, either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	Copyright (C) 2012-2014 PIVA SOFTWARE (www.pivasoftware.com)
 *		Author: Mohamed Kallel <mohamed.kallel@pivasoftware.com>
 *		Author: Anis Ellouze <anis.ellouze@pivasoftware.com>
 *	Copyright (C) 2011 Luka Perkov <freecwmp@lukaperkov.net>
 */

#include <errno.h>
#include <malloc.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <libubox/uloop.h>
#ifdef JSONC
 #include <json-c/json.h>
#else
 #include <json/json.h>
#endif


#include "external.h"
#include "easycwmp.h"
#include "log.h"

#define OPT(x) x?x:""

LIST_HEAD(external_list_parameter);
char *external_method_status = NULL;
char *external_method_instance = NULL;
char *external_method_fault = NULL;
static int pfds_in[2], pfds_out[2], pid;

#define STATICW

#include <execinfo.h>

void bt() {
#define BUFF_SIZE 100
	int j, nptrs;
	void *buffer[BUFF_SIZE];
	char **strings;

	nptrs = backtrace(buffer, BUFF_SIZE);
	//printf("bt:+ nptrs=%d *****\n", nptrs);

	strings = backtrace_symbols(buffer, nptrs);
	//backtrace_symbols_fd(buffer + 1, nptrs - 1, STDOUT_FILENO);
	if (strings == NULL) {
		perror("backtrace_symbols)");
		exit(EXIT_FAILURE);
	}
	for (j = 1; j < nptrs; j++) {
		printf(" ** %2d %s\n", nptrs - j - 1, strings[j]);
	}
	free(strings);
	//printf("bt:- nptrs=%d *****\n", nptrs);
}

void external_add_list_paramameter(char *param_name, char *param_data, char *param_type, char *fault_code)
{
	struct external_parameter *external_parameter;
	struct list_head *ilist;

	if (!param_name) param_name = "";
	list_for_each(ilist, &external_list_parameter) {
		external_parameter = list_entry(ilist, struct external_parameter, list);
		int cmp = strcmp(external_parameter->name, param_name);
		if (cmp == 0) {
			if (!external_parameter->fault_code || external_parameter->fault_code[0] == '\0') {
				return;
			}
			else {
				break;
			}
		}
	}
	external_parameter = calloc(1, sizeof(struct external_parameter));
	list_add_tail(&external_parameter->list, &external_list_parameter);
	external_parameter->name = strdup(param_name);
	if (param_data) external_parameter->data = strdup(param_data);
	if (param_type) external_parameter->type = strdup(param_type);
	if (fault_code) external_parameter->fault_code = strdup(fault_code);
}

void external_parameter_delete(struct external_parameter *external_parameter)
{
	list_del(&external_parameter->list);
	free(external_parameter->name);
	free(external_parameter->data);
	free(external_parameter->type);
	free(external_parameter->fault_code);
	free(external_parameter);
}

void external_free_list_parameter()
{
	struct external_parameter *external_parameter;
	while (external_list_parameter.next != &external_list_parameter) {
		external_parameter = list_entry(external_list_parameter.next, struct external_parameter, list);
		external_parameter_delete(external_parameter);
	}
}

void external_set_param_resp_status(char *status)
{
	free(external_method_status);
	external_method_status = status ? strdup(status) : NULL;
}

void external_fetch_set_param_resp_status(char **status)
{
	*status = external_method_status;
	external_method_status = NULL;
}

void external_method_resp_status (char *status, char *fault)
{
	free(external_method_status);
	external_method_status = status ? strdup(status) : NULL;
	free(external_method_fault);
	external_method_fault = fault ? strdup(fault) : NULL;
}

void external_fetch_method_resp_status (char **status, char **fault)
{
	*status = external_method_status;
	external_method_status = NULL;
	*fault = external_method_fault;
	external_method_fault = NULL;
}

void external_add_obj_resp (char *status, char *instance, char *fault)
{
	free(external_method_status);
	external_method_status = status ? strdup(status) : NULL;
	free(external_method_instance);
	external_method_instance = instance ? strdup(instance) : NULL;
	free(external_method_fault);
	external_method_fault = fault ? strdup(fault) : NULL;
}

void external_fetch_add_obj_resp (char **status, char **instance, char **fault)
{
	*status = external_method_status;
	external_method_status = NULL;
	*instance = external_method_instance;
	external_method_instance = NULL;
	*fault = external_method_fault;
	external_method_fault = NULL;
}

STATICW int external_read_pipe(int (*json_handle)(char *))
{
	D("+ pfds_out[0]=%d\n", pfds_out[0]);
	char buffer[1];
	ssize_t rxed;
	char *c = NULL, *line = NULL;
	int t;
	while ((rxed = read(pfds_out[0], buffer, sizeof(buffer))) > 0) {
		if (buffer[0] == '\n') {
			if (line == NULL) continue;
			if (strcmp(line, EXTERNAL_PROMPT) == 0) {
				D(" done, saw %s\n", line);
				goto done;
			}
			if (json_handle) {
				D(" read LF json_handle=%p line='%s'\n", json_handle, line);
        bt();
				json_handle(line);
			}
			FREE(line);
		}
		else {
			if (line)
				t = asprintf(&c, "%s%c", line, buffer[0]);
			else
				t = asprintf(&c, "%c", buffer[0]);

			if (t == -1) {
				D(" error %d\n", errno);
				goto error;
			}

			free(line);
			line = c;
		}
	}
	D(" rxed=%d done\n", rxed);

done:
	free(line);
	D("- pfds_out[0]=%d OK\n", pfds_out[0]);
	return 0;

error:
	free(c);
	free(line);
	D("- pfds_out[0]=%d error\n", pfds_out[0]);
	return -1;
}

STATICW void external_write_pipe(const char *msg)
{
	D("+ pfds_in[1]=%d msg=%s\n", pfds_in[1], msg);
	char *value = NULL;
	if(asprintf(&value, "%s\n", msg) == -1) return;
	if (write(pfds_in[1], value, strlen(value)) == -1) {
		log_message(NAME, L_CRIT, "errno=%d:%s; occured when trying to write to pfds_in[1]=%d\n", errno, strerror(errno), pfds_in[1]);
		bt();
	}
	free(value);
	D("- pfds_in[1]=%d msg=%s\n", pfds_in[1], msg);
}

STATICW void external_add_json_obj(json_object *json_obj_out, char *object, char *string)
{
	json_object *json_obj_tmp = json_object_new_string(string);
	json_object_object_add(json_obj_out, object, json_obj_tmp);
}

int external_init()
{
	D("+\n");
	log_message(NAME, L_NOTICE, "external script init\n");
	if (pipe(pfds_out) < 0) {
		D("- pipe pfds_out error\n");
		return -1;
	}
	if (pipe(pfds_in) < 0) {
		D("- pipe pfds_in error\n");
		return -1;
	}
	if ((pid = fork()) == -1) {
		log_message(NAME, L_CRIT, "external init fork failed\n");
		return -1;
	}

	if (pid == 0) {
		/* child */
		D(" child\n");
		close(pfds_out[0]);
		dup2(pfds_out[1], STDOUT_FILENO);
		close(pfds_out[1]);

		close(pfds_in[1]);
		dup2(pfds_in[0], STDIN_FILENO);
		close(pfds_in[0]);

		// Count number of of elements in environ
		size_t environ_len = 0;
		while(environ[environ_len] != NULL) {
			environ_len += 1;
		}

		// Add the additional space we need and env on the stack
		const size_t additional_envs = 2;
		environ_len += additional_envs;
		char *env[environ_len + 1]; // One extra for the NULL

		// Add the new env vars and copy current values
		int i = 0;
		asprintf((char **)&env[i++], "EASYCWMP_INSTALL_DIR=%s", easycwmp_install_dir);
		asprintf((char **)&env[i++], "LD_LIBRARY_PATH=%s/lib", easycwmp_install_dir);
		char *e = NULL;
		int j = 0;
		do {
			e = env[i++] = environ[j++];
		} while(e != NULL);

		// Create argv array
		i=0;
		const char *argv[4];
		asprintf((char **)&argv[i++], "%s%s", easycwmp_install_dir, fc_script);
		argv[i++] = "--json-input";
		argv[i++] = NULL;

		//char **s;
		//for (i = 0, s = (char **)&argv[0]; *s != NULL; s++, i++) D("argv[%d]=%s\n", i, *s);
		//for (i = 0, s = (char **)&env[0]; *s != NULL; s++, i++) D("env[%d]=%s\n", i, *s);

		D("child START argv[0]=%s\n", argv[0]);
		execvpe(argv[0], (char **)argv, (char **)env);
		D("child START ERROR errno=%d:%s fc_script=%s\n", argv[0], errno, strerror(errno));
		exit(ESRCH);
	} else if (pid < 0) {
		D("- pic < 0 error\n");
		log_message(NAME, L_CRIT, "external_init: pid < -1\n");
		return -1;
	}

	D("parent, closing pfds_out[1]=%d and pfds_in[0]=%d\n", pfds_out[1], pfds_in[0]);
	int result;
	result = close(pfds_out[1]);
	if (result == -1) {
		D("parent, error %d closing pfds_out[1]\n", errno);
	}
	result = close(pfds_in[0]);
	if (result == -1) {
		D("parent, error %d closing pfds_in[0]\n", errno);
	}

	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR)
		log_message(NAME, L_CRIT, "ignoring pipe signal failed\n");

	int r = external_read_pipe(NULL);

	D("- parent r=%d read pipe result\n", r);
	return r;
}

void external_exit()
{
	log_message(NAME, L_NOTICE, "external script exit\n");

	json_object *json_obj_out = json_object_new_object();
	external_add_json_obj(json_obj_out, "command", "exit");
	external_write_pipe(json_object_to_json_string(json_obj_out));
	json_object_put(json_obj_out);
	external_read_pipe(NULL);
	int status;
	while (wait(&status) != pid) {
		DD("waiting for child to exit");
	}
	close(pfds_out[0]);
	close(pfds_in[1]);
}

int external_action_parameter_execute(char *command, char *class, char *name, char *arg)
{
	D("+ command=%s class=%s name=%s arg=%s\n", command, class?class:"", name?name:"", arg?arg:"");
	//log_message(NAME, L_NOTICE, "external: execute %s %s %s %s\n",
	//		command, class, name, arg?arg:"");

	json_object *json_obj_out = json_object_new_object();
	external_add_json_obj(json_obj_out, "command", command);
	external_add_json_obj(json_obj_out, "class", class);
	external_add_json_obj(json_obj_out, "parameter", name);
	if (arg) external_add_json_obj(json_obj_out, "argument", arg);
	external_write_pipe(json_object_to_json_string(json_obj_out));
	json_object_put(json_obj_out);

	D("- command=%s class=%s name=%s arg=%s\n", command, class?class:"", name?name:"", arg?arg:"");
	return 0;
}

int external_action_simple_execute(char *command, char *class, char *arg)
{
	D("+ command=%s class=%s arg=%s\n", command, OPT(class), OPT(arg));
	//log_message(NAME, L_NOTICE, "external: execute %s %s %s\n",
	//		command, class?class:"", arg?arg:"");

	json_object *json_obj_out = json_object_new_object();
	external_add_json_obj(json_obj_out, "command", command);
	if (class) external_add_json_obj(json_obj_out, "class", class);
	if (arg) external_add_json_obj(json_obj_out, "argument", arg);
	external_write_pipe(json_object_to_json_string(json_obj_out));
	json_object_put(json_obj_out);

	D("- command=%s class=%s arg=%s\n", command, OPT(class), OPT(arg));
	return 0;
}

int external_action_download_execute(char *url, char *file_type, char *file_size, char *user_name, char *password)
{
	D("+ url==%s file_type==%s file_size=%s user_name=%s password=%s\n", OPT(url), OPT(file_type), OPT(file_size), OPT(user_name), OPT(password));
	//log_message(NAME, L_NOTICE, "external: execute download %s\n", url);

	json_object *json_obj_out = json_object_new_object();
	external_add_json_obj(json_obj_out, "command", "download");
	external_add_json_obj(json_obj_out, "url", url);
	external_add_json_obj(json_obj_out, "file_type", file_type);
	if (file_size) external_add_json_obj(json_obj_out, "file_size", file_size);
	if (user_name) external_add_json_obj(json_obj_out, "user_name", user_name);
	if (password) external_add_json_obj(json_obj_out, "password", password);
	external_write_pipe(json_object_to_json_string(json_obj_out));
	json_object_put(json_obj_out);

	D("- url==%s file_type==%s file_size=%s user_name=%s password=%s\n", OPT(url), OPT(file_type), OPT(file_size), OPT(user_name), OPT(password));
	return 0;
}

int external_action_upload_execute(char *url, char *file_type, char *user_name, char *password)
{
	D("+ url==%s file_type==%s user_name=%s password=%s\n", OPT(url), OPT(file_type), OPT(user_name), OPT(password));
	//log_message(NAME, L_NOTICE, "external: execute upload %s\n", url);

	json_object *json_obj_out = json_object_new_object();
	external_add_json_obj(json_obj_out, "command", "upload");
	external_add_json_obj(json_obj_out, "url", url);
	external_add_json_obj(json_obj_out, "file_type", file_type);
	if (user_name) external_add_json_obj(json_obj_out, "user_name", user_name);
	if (password) external_add_json_obj(json_obj_out, "password", password);
	external_write_pipe(json_object_to_json_string(json_obj_out));
	json_object_put(json_obj_out);

	D("- url==%s file_type==%s user_name=%s password=%s\n", OPT(url), OPT(file_type), OPT(user_name), OPT(password));
	return 0;
}

int external_action_handle (int (*json_handle)(char *))
{
	D("+ json_handle=%p\n", json_handle);

	json_object *json_obj_out = json_object_new_object();
	external_add_json_obj(json_obj_out, "command", "end");
	external_write_pipe(json_object_to_json_string(json_obj_out));
	json_object_put(json_obj_out);

	int r = external_read_pipe(json_handle);
	D("- json_handle=%p\n", json_handle);
	return r;
}
