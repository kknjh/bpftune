/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2023, Oracle and/or its affiliates. */

#include <libbpftune.h>
#include "sysctl_tuner.skel.h"
#include "sysctl_tuner.skel.legacy.h"

#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

int init(struct bpftuner *tuner)
{
	int prog_fd, cgroup_fd, err;
	struct bpf_program *prog;
	const char *cgroup_dir;

	bpftuner_bpf_init(sysctl, tuner);

	/* attach to root cgroup */
	cgroup_dir = bpftune_cgroup_name();

	if (!cgroup_dir) {
		bpftune_log(LOG_ERR, "cannot get cgroup_dir\n");
		return 1;
	}
	cgroup_fd = bpftune_cgroup_fd();
	prog = bpf_object__find_program_by_name(tuner->obj, "sysctl_write");
	if (!prog) {
		bpftune_log(LOG_ERR, "no prog 'sysctl_write'\n");
		return -EINVAL;
	}
	prog_fd = bpf_program__fd(prog);

	if (bpf_prog_attach(prog_fd, cgroup_fd,
			    BPF_CGROUP_SYSCTL, BPF_F_ALLOW_MULTI)) {
		err = -errno;
		bpftune_log(LOG_ERR, "cannot attach to cgroup '%s': %s\n",
			    cgroup_dir, strerror(-err));
		return 1;
	}
	bpftune_log(LOG_DEBUG, "attached prog fd %d to cgroup fd %d\n",
		    prog_fd, cgroup_fd);

	return 0;
}

void fini(struct bpftuner *tuner)
{
	struct bpf_program *prog;
	int err, prog_fd, cgroup_fd;

	bpftune_log(LOG_DEBUG, "calling fini for %s\n", tuner->name);
	prog = bpf_object__find_program_by_name(tuner->obj, "sysctl_write");
	if (prog) {
		prog_fd = bpf_program__fd(prog);
		cgroup_fd = bpftune_cgroup_fd();

		if (bpf_prog_detach2(prog_fd, cgroup_fd, BPF_CGROUP_SYSCTL)) {
			err = -errno;
			bpftune_log(LOG_ERR, "error detaching prog fd %d, cgroup fd %d: %s\n",
				prog_fd, cgroup_fd, strerror(-err));
		}
	}
	bpftuner_bpf_fini(tuner);
}

void event_handler(struct bpftuner *tuner, struct bpftune_event *event,
		   __attribute__((unused))void *ctx)
{
	struct bpftuner *t = NULL;

	bpftune_log(LOG_DEBUG, "sysctl write for '%s' (scenario %d) for tuner %s\n",
		    event->str, event->scenario_id, tuner->name);

	bpftune_for_each_tuner(t) {
		struct bpftunable *tunable;

		bpftune_log(LOG_DEBUG, "checking tuner %s\n", tuner->name);
		bpftuner_for_each_tunable(t, tunable) {
			char path[512];

			bpftune_sysctl_name_to_path(tunable->desc.name, path,
						    sizeof(path));

			bpftune_log(LOG_DEBUG, "checking path %s against %s\n",
				    path, event->str);
			if (strstr(path, event->str)) {
				bpftune_log(LOG_INFO,
					    "user modified sysctl '%s' that tuner '%s' uses; disabling '%s'!\n",
					    event->str, t->name, t->name);
				bpftuner_fini(t, BPFTUNE_MANUAL);
				break;
			}
		}
	}
}
