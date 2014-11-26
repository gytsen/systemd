/*-*- Mode: C; c-basic-offset: 8; indent-tabs-mode: nil -*-*/

/***
  This file is part of systemd.

  Copyright 2013 Lennart Poettering

  systemd is free software; you can redistribute it and/or modify it
  under the terms of the GNU Lesser General Public License as published by
  the Free Software Foundation; either version 2.1 of the License, or
  (at your option) any later version.

  systemd is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with systemd; If not, see <http://www.gnu.org/licenses/>.
***/

#include <stdlib.h>

#include "util.h"
#include "cgroup-util.h"
#include "fileio.h"
#include "audit.h"
#include "bus-message.h"
#include "bus-util.h"
#include "time-util.h"
#include "strv.h"
#include "bus-creds.h"
#include "bus-label.h"

enum {
        CAP_OFFSET_INHERITABLE = 0,
        CAP_OFFSET_PERMITTED = 1,
        CAP_OFFSET_EFFECTIVE = 2,
        CAP_OFFSET_BOUNDING = 3
};

void bus_creds_done(sd_bus_creds *c) {
        assert(c);

        /* For internal bus cred structures that are allocated by
         * something else */

        free(c->session);
        free(c->unit);
        free(c->user_unit);
        free(c->slice);
        free(c->unescaped_description);

        strv_free(c->cmdline_array);
        strv_free(c->well_known_names);
}

_public_ sd_bus_creds *sd_bus_creds_ref(sd_bus_creds *c) {
        assert_return(c, NULL);

        if (c->allocated) {
                assert(c->n_ref > 0);
                c->n_ref++;
        } else {
                sd_bus_message *m;

                /* If this is an embedded creds structure, then
                 * forward ref counting to the message */
                m = container_of(c, sd_bus_message, creds);
                sd_bus_message_ref(m);
        }

        return c;
}

_public_ sd_bus_creds *sd_bus_creds_unref(sd_bus_creds *c) {

        if (!c)
                return NULL;

        if (c->allocated) {
                assert(c->n_ref > 0);
                c->n_ref--;

                if (c->n_ref == 0) {
                        bus_creds_done(c);

                        free(c->comm);
                        free(c->tid_comm);
                        free(c->exe);
                        free(c->cmdline);
                        free(c->cgroup);
                        free(c->capability);
                        free(c->label);
                        free(c->unique_name);
                        free(c->cgroup_root);
                        free(c->description);
                        free(c->supplementary_gids);
                        free(c);
                }
        } else {
                sd_bus_message *m;

                m = container_of(c, sd_bus_message, creds);
                sd_bus_message_unref(m);
        }


        return NULL;
}

_public_ uint64_t sd_bus_creds_get_mask(const sd_bus_creds *c) {
        assert_return(c, 0);

        return c->mask;
}

sd_bus_creds* bus_creds_new(void) {
        sd_bus_creds *c;

        c = new0(sd_bus_creds, 1);
        if (!c)
                return NULL;

        c->allocated = true;
        c->n_ref = 1;
        return c;
}

_public_ int sd_bus_creds_new_from_pid(sd_bus_creds **ret, pid_t pid, uint64_t mask) {
        sd_bus_creds *c;
        int r;

        assert_return(pid >= 0, -EINVAL);
        assert_return(mask <= _SD_BUS_CREDS_ALL, -ENOTSUP);
        assert_return(ret, -EINVAL);

        if (pid == 0)
                pid = getpid();

        c = bus_creds_new();
        if (!c)
                return -ENOMEM;

        r = bus_creds_add_more(c, mask | SD_BUS_CREDS_AUGMENT, pid, 0);
        if (r < 0) {
                sd_bus_creds_unref(c);
                return r;
        }

        /* Check if the process existed at all, in case we haven't
         * figured that out already */
        if (!pid_is_alive(pid)) {
                sd_bus_creds_unref(c);
                return -ESRCH;
        }

        *ret = c;
        return 0;
}

_public_ int sd_bus_creds_get_uid(sd_bus_creds *c, uid_t *uid) {
        assert_return(c, -EINVAL);
        assert_return(uid, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_UID))
                return -ENODATA;

        *uid = c->uid;
        return 0;
}

_public_ int sd_bus_creds_get_euid(sd_bus_creds *c, uid_t *euid) {
        assert_return(c, -EINVAL);
        assert_return(euid, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_EUID))
                return -ENODATA;

        *euid = c->euid;
        return 0;
}

_public_ int sd_bus_creds_get_suid(sd_bus_creds *c, uid_t *suid) {
        assert_return(c, -EINVAL);
        assert_return(suid, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_SUID))
                return -ENODATA;

        *suid = c->suid;
        return 0;
}


_public_ int sd_bus_creds_get_fsuid(sd_bus_creds *c, uid_t *fsuid) {
        assert_return(c, -EINVAL);
        assert_return(fsuid, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_FSUID))
                return -ENODATA;

        *fsuid = c->fsuid;
        return 0;
}

_public_ int sd_bus_creds_get_gid(sd_bus_creds *c, gid_t *gid) {
        assert_return(c, -EINVAL);
        assert_return(gid, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_UID))
                return -ENODATA;

        *gid = c->gid;
        return 0;
}


_public_ int sd_bus_creds_get_egid(sd_bus_creds *c, gid_t *egid) {
        assert_return(c, -EINVAL);
        assert_return(egid, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_EGID))
                return -ENODATA;

        *egid = c->egid;
        return 0;
}

_public_ int sd_bus_creds_get_sgid(sd_bus_creds *c, gid_t *sgid) {
        assert_return(c, -EINVAL);
        assert_return(sgid, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_SGID))
                return -ENODATA;

        *sgid = c->sgid;
        return 0;
}

_public_ int sd_bus_creds_get_fsgid(sd_bus_creds *c, gid_t *fsgid) {
        assert_return(c, -EINVAL);
        assert_return(fsgid, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_FSGID))
                return -ENODATA;

        *fsgid = c->fsgid;
        return 0;
}

_public_ int sd_bus_creds_get_supplementary_gids(sd_bus_creds *c, const gid_t **gids) {
        assert_return(c, -EINVAL);
        assert_return(gids, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_SUPPLEMENTARY_GIDS))
                return -ENODATA;

        *gids = c->supplementary_gids;
        return (int) c->n_supplementary_gids;
}

_public_ int sd_bus_creds_get_pid(sd_bus_creds *c, pid_t *pid) {
        assert_return(c, -EINVAL);
        assert_return(pid, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_PID))
                return -ENODATA;

        assert(c->pid > 0);
        *pid = c->pid;
        return 0;
}

_public_ int sd_bus_creds_get_tid(sd_bus_creds *c, pid_t *tid) {
        assert_return(c, -EINVAL);
        assert_return(tid, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_TID))
                return -ENODATA;

        assert(c->tid > 0);
        *tid = c->tid;
        return 0;
}

_public_ int sd_bus_creds_get_pid_starttime(sd_bus_creds *c, uint64_t *usec) {
        assert_return(c, -EINVAL);
        assert_return(usec, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_PID_STARTTIME))
                return -ENODATA;

        assert(c->pid_starttime > 0);
        *usec = c->pid_starttime;
        return 0;
}

_public_ int sd_bus_creds_get_selinux_context(sd_bus_creds *c, const char **ret) {
        assert_return(c, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_SELINUX_CONTEXT))
                return -ENODATA;

        assert(c->label);
        *ret = c->label;
        return 0;
}

_public_ int sd_bus_creds_get_comm(sd_bus_creds *c, const char **ret) {
        assert_return(c, -EINVAL);
        assert_return(ret, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_COMM))
                return -ENODATA;

        assert(c->comm);
        *ret = c->comm;
        return 0;
}

_public_ int sd_bus_creds_get_tid_comm(sd_bus_creds *c, const char **ret) {
        assert_return(c, -EINVAL);
        assert_return(ret, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_TID_COMM))
                return -ENODATA;

        assert(c->tid_comm);
        *ret = c->tid_comm;
        return 0;
}

_public_ int sd_bus_creds_get_exe(sd_bus_creds *c, const char **ret) {
        assert_return(c, -EINVAL);
        assert_return(ret, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_EXE))
                return -ENODATA;

        assert(c->exe);
        *ret = c->exe;
        return 0;
}

_public_ int sd_bus_creds_get_cgroup(sd_bus_creds *c, const char **ret) {
        assert_return(c, -EINVAL);
        assert_return(ret, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_CGROUP))
                return -ENODATA;

        assert(c->cgroup);
        *ret = c->cgroup;
        return 0;
}

_public_ int sd_bus_creds_get_unit(sd_bus_creds *c, const char **ret) {
        int r;

        assert_return(c, -EINVAL);
        assert_return(ret, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_UNIT))
                return -ENODATA;

        assert(c->cgroup);

        if (!c->unit) {
                const char *shifted;

                r = cg_shift_path(c->cgroup, c->cgroup_root, &shifted);
                if (r < 0)
                        return r;

                r = cg_path_get_unit(shifted, (char**) &c->unit);
                if (r < 0)
                        return r;
        }

        *ret = c->unit;
        return 0;
}

_public_ int sd_bus_creds_get_user_unit(sd_bus_creds *c, const char **ret) {
        int r;

        assert_return(c, -EINVAL);
        assert_return(ret, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_USER_UNIT))
                return -ENODATA;

        assert(c->cgroup);

        if (!c->user_unit) {
                const char *shifted;

                r = cg_shift_path(c->cgroup, c->cgroup_root, &shifted);
                if (r < 0)
                        return r;

                r = cg_path_get_user_unit(shifted, (char**) &c->user_unit);
                if (r < 0)
                        return r;
        }

        *ret = c->user_unit;
        return 0;
}

_public_ int sd_bus_creds_get_slice(sd_bus_creds *c, const char **ret) {
        int r;

        assert_return(c, -EINVAL);
        assert_return(ret, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_SLICE))
                return -ENODATA;

        assert(c->cgroup);

        if (!c->slice) {
                const char *shifted;

                r = cg_shift_path(c->cgroup, c->cgroup_root, &shifted);
                if (r < 0)
                        return r;

                r = cg_path_get_slice(shifted, (char**) &c->slice);
                if (r < 0)
                        return r;
        }

        *ret = c->slice;
        return 0;
}

_public_ int sd_bus_creds_get_session(sd_bus_creds *c, const char **ret) {
        int r;

        assert_return(c, -EINVAL);
        assert_return(ret, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_SESSION))
                return -ENODATA;

        assert(c->cgroup);

        if (!c->session) {
                const char *shifted;

                r = cg_shift_path(c->cgroup, c->cgroup_root, &shifted);
                if (r < 0)
                        return r;

                r = cg_path_get_session(shifted, (char**) &c->session);
                if (r < 0)
                        return r;
        }

        *ret = c->session;
        return 0;
}

_public_ int sd_bus_creds_get_owner_uid(sd_bus_creds *c, uid_t *uid) {
        const char *shifted;
        int r;

        assert_return(c, -EINVAL);
        assert_return(uid, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_OWNER_UID))
                return -ENODATA;

        assert(c->cgroup);

        r = cg_shift_path(c->cgroup, c->cgroup_root, &shifted);
        if (r < 0)
                return r;

        return cg_path_get_owner_uid(shifted, uid);
}

_public_ int sd_bus_creds_get_cmdline(sd_bus_creds *c, char ***cmdline) {
        assert_return(c, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_CMDLINE))
                return -ENODATA;

        assert_return(c->cmdline, -ESRCH);
        assert(c->cmdline);

        if (!c->cmdline_array) {
                c->cmdline_array = strv_parse_nulstr(c->cmdline, c->cmdline_size);
                if (!c->cmdline_array)
                        return -ENOMEM;
        }

        *cmdline = c->cmdline_array;
        return 0;
}

_public_ int sd_bus_creds_get_audit_session_id(sd_bus_creds *c, uint32_t *sessionid) {
        assert_return(c, -EINVAL);
        assert_return(sessionid, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_AUDIT_SESSION_ID))
                return -ENODATA;

        *sessionid = c->audit_session_id;
        return 0;
}

_public_ int sd_bus_creds_get_audit_login_uid(sd_bus_creds *c, uid_t *uid) {
        assert_return(c, -EINVAL);
        assert_return(uid, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_AUDIT_LOGIN_UID))
                return -ENODATA;

        *uid = c->audit_login_uid;
        return 0;
}

_public_ int sd_bus_creds_get_unique_name(sd_bus_creds *c, const char **unique_name) {
        assert_return(c, -EINVAL);
        assert_return(unique_name, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_UNIQUE_NAME))
                return -ENODATA;

        *unique_name = c->unique_name;
        return 0;
}

_public_ int sd_bus_creds_get_well_known_names(sd_bus_creds *c, char ***well_known_names) {
        assert_return(c, -EINVAL);
        assert_return(well_known_names, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_WELL_KNOWN_NAMES))
                return -ENODATA;

        *well_known_names = c->well_known_names;
        return 0;
}

_public_ int sd_bus_creds_get_description(sd_bus_creds *c, const char **ret) {
        assert_return(c, -EINVAL);
        assert_return(ret, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_DESCRIPTION))
                return -ENODATA;

        assert(c->description);

        if (!c->unescaped_description) {
                c->unescaped_description = bus_label_unescape(c->description);
                if (!c->unescaped_description)
                        return -ENOMEM;
        }

        *ret = c->unescaped_description;
        return 0;
}

static int has_cap(sd_bus_creds *c, unsigned offset, int capability) {
        size_t sz;

        assert(c);
        assert(c->capability);

        sz = c->capability_size / 4;
        if ((size_t) capability >= sz*8)
                return 0;

        return !!(c->capability[offset * sz + (capability / 8)] & (1 << (capability % 8)));
}

_public_ int sd_bus_creds_has_effective_cap(sd_bus_creds *c, int capability) {
        assert_return(c, -EINVAL);
        assert_return(capability >= 0, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_EFFECTIVE_CAPS))
                return -ENODATA;

        return has_cap(c, CAP_OFFSET_EFFECTIVE, capability);
}

_public_ int sd_bus_creds_has_permitted_cap(sd_bus_creds *c, int capability) {
        assert_return(c, -EINVAL);
        assert_return(capability >= 0, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_PERMITTED_CAPS))
                return -ENODATA;

        return has_cap(c, CAP_OFFSET_PERMITTED, capability);
}

_public_ int sd_bus_creds_has_inheritable_cap(sd_bus_creds *c, int capability) {
        assert_return(c, -EINVAL);
        assert_return(capability >= 0, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_INHERITABLE_CAPS))
                return -ENODATA;

        return has_cap(c, CAP_OFFSET_INHERITABLE, capability);
}

_public_ int sd_bus_creds_has_bounding_cap(sd_bus_creds *c, int capability) {
        assert_return(c, -EINVAL);
        assert_return(capability >= 0, -EINVAL);

        if (!(c->mask & SD_BUS_CREDS_BOUNDING_CAPS))
                return -ENODATA;

        return has_cap(c, CAP_OFFSET_BOUNDING, capability);
}

static int parse_caps(sd_bus_creds *c, unsigned offset, const char *p) {
        size_t sz;
        unsigned i;

        assert(c);
        assert(p);

        p += strspn(p, WHITESPACE);

        sz = strlen(p);
        if (sz % 2 != 0)
                return -EINVAL;

        sz /= 2;
        if (!c->capability) {
                c->capability = new0(uint8_t, sz * 4);
                if (!c->capability)
                        return -ENOMEM;

                c->capability_size = sz * 4;
        }

        for (i = 0; i < sz; i ++) {
                int x, y;

                x = unhexchar(p[i*2]);
                y = unhexchar(p[i*2+1]);

                if (x < 0 || y < 0)
                        return -EINVAL;

                c->capability[offset * sz + (sz - i - 1)] = (uint8_t) x << 4 | (uint8_t) y;
        }

        return 0;
}

int bus_creds_add_more(sd_bus_creds *c, uint64_t mask, pid_t pid, pid_t tid) {
        uint64_t missing;
        int r;

        assert(c);
        assert(c->allocated);

        if (!(mask & SD_BUS_CREDS_AUGMENT))
                return 0;

        missing = mask & ~c->mask;
        if (missing == 0)
                return 0;

        /* Try to retrieve PID from creds if it wasn't passed to us */
        if (pid <= 0 && (c->mask & SD_BUS_CREDS_PID))
                pid = c->pid;

        if (tid <= 0 && (c->mask & SD_BUS_CREDS_TID))
                tid = c->pid;

        /* Without pid we cannot do much... */
        if (pid <= 0)
                return 0;

        if (pid > 0) {
                c->pid = pid;
                c->mask |= SD_BUS_CREDS_PID;
        }

        if (tid > 0) {
                c->tid = tid;
                c->mask |= SD_BUS_CREDS_TID;
        }

        if (missing & (SD_BUS_CREDS_UID | SD_BUS_CREDS_EUID | SD_BUS_CREDS_SUID | SD_BUS_CREDS_FSUID |
                       SD_BUS_CREDS_GID | SD_BUS_CREDS_EGID | SD_BUS_CREDS_SGID | SD_BUS_CREDS_FSGID |
                       SD_BUS_CREDS_SUPPLEMENTARY_GIDS |
                       SD_BUS_CREDS_EFFECTIVE_CAPS | SD_BUS_CREDS_INHERITABLE_CAPS |
                       SD_BUS_CREDS_PERMITTED_CAPS | SD_BUS_CREDS_BOUNDING_CAPS)) {

                _cleanup_fclose_ FILE *f = NULL;
                const char *p;

                p = procfs_file_alloca(pid, "status");

                f = fopen(p, "re");
                if (!f) {
                        if (errno == ENOENT)
                                return -ESRCH;
                        else if (errno != EPERM && errno != EACCES)
                                return -errno;
                } else {
                        char line[LINE_MAX];

                        FOREACH_LINE(line, f, return -errno) {
                                truncate_nl(line);

                                if (missing & (SD_BUS_CREDS_UID|SD_BUS_CREDS_EUID|SD_BUS_CREDS_SUID|SD_BUS_CREDS_FSUID)) {
                                        p = startswith(line, "Uid:");
                                        if (p) {
                                                unsigned long uid, euid, suid, fsuid;

                                                p += strspn(p, WHITESPACE);
                                                if (sscanf(p, "%lu %lu %lu %lu", &uid, &euid, &suid, &fsuid) != 4)
                                                        return -EIO;

                                                c->uid = (uid_t) uid;
                                                c->euid = (uid_t) euid;
                                                c->suid = (uid_t) suid;
                                                c->fsuid = (uid_t) fsuid;
                                                c->mask |= missing & (SD_BUS_CREDS_UID|SD_BUS_CREDS_EUID|SD_BUS_CREDS_SUID|SD_BUS_CREDS_FSUID);
                                                continue;
                                        }
                                }

                                if (missing & (SD_BUS_CREDS_GID|SD_BUS_CREDS_EGID|SD_BUS_CREDS_SGID|SD_BUS_CREDS_FSGID)) {
                                        p = startswith(line, "Gid:");
                                        if (p) {
                                                unsigned long gid, egid, sgid, fsgid;

                                                p += strspn(p, WHITESPACE);
                                                if (sscanf(p, "%lu %lu %lu %lu", &gid, &egid, &sgid, &fsgid) != 4)
                                                        return -EIO;

                                                c->gid = (gid_t) gid;
                                                c->egid = (gid_t) egid;
                                                c->sgid = (gid_t) sgid;
                                                c->fsgid = (gid_t) fsgid;
                                                c->mask |= missing & (SD_BUS_CREDS_GID|SD_BUS_CREDS_EGID|SD_BUS_CREDS_SGID|SD_BUS_CREDS_FSGID);
                                                continue;
                                        }
                                }

                                if (missing & SD_BUS_CREDS_SUPPLEMENTARY_GIDS) {
                                        p = startswith(line, "Groups:");
                                        if (p) {
                                                size_t allocated = 0;

                                                for (;;) {
                                                        unsigned long g;
                                                        int n = 0;

                                                        p += strspn(p, WHITESPACE);
                                                        if (*p == 0)
                                                                break;

                                                        if (sscanf(p, "%lu%n", &g, &n) != 1)
                                                                return -EIO;

                                                        if (!GREEDY_REALLOC(c->supplementary_gids, allocated, c->n_supplementary_gids+1))
                                                                return -ENOMEM;

                                                        c->supplementary_gids[c->n_supplementary_gids++] = (gid_t) g;
                                                        p += n;
                                                }

                                                c->mask |= SD_BUS_CREDS_SUPPLEMENTARY_GIDS;
                                                continue;
                                        }
                                }

                                if (missing & SD_BUS_CREDS_EFFECTIVE_CAPS) {
                                        p = startswith(line, "CapEff:");
                                        if (p) {
                                                r = parse_caps(c, CAP_OFFSET_EFFECTIVE, p);
                                                if (r < 0)
                                                        return r;

                                                c->mask |= SD_BUS_CREDS_EFFECTIVE_CAPS;
                                                continue;
                                        }
                                }

                                if (missing & SD_BUS_CREDS_PERMITTED_CAPS) {
                                        p = startswith(line, "CapPrm:");
                                        if (p) {
                                                r = parse_caps(c, CAP_OFFSET_PERMITTED, p);
                                                if (r < 0)
                                                        return r;

                                                c->mask |= SD_BUS_CREDS_PERMITTED_CAPS;
                                                continue;
                                        }
                                }

                                if (missing & SD_BUS_CREDS_INHERITABLE_CAPS) {
                                        p = startswith(line, "CapInh:");
                                        if (p) {
                                                r = parse_caps(c, CAP_OFFSET_INHERITABLE, p);
                                                if (r < 0)
                                                        return r;

                                                c->mask |= SD_BUS_CREDS_INHERITABLE_CAPS;
                                                continue;
                                        }
                                }

                                if (missing & SD_BUS_CREDS_BOUNDING_CAPS) {
                                        p = startswith(line, "CapBnd:");
                                        if (p) {
                                                r = parse_caps(c, CAP_OFFSET_BOUNDING, p);
                                                if (r < 0)
                                                        return r;

                                                c->mask |= SD_BUS_CREDS_BOUNDING_CAPS;
                                                continue;
                                        }
                                }
                        }
                }
        }

        if (missing & (SD_BUS_CREDS_PID_STARTTIME)) {
                unsigned long long st;

                r = get_starttime_of_pid(pid, &st);
                if (r < 0) {
                        if (r != -EPERM && r != -EACCES)
                                return r;
                } else {
                        c->pid_starttime = ((usec_t) st * USEC_PER_SEC) / (usec_t) sysconf(_SC_CLK_TCK);
                        c->mask |= SD_BUS_CREDS_PID_STARTTIME;
                }
        }

        if (missing & SD_BUS_CREDS_SELINUX_CONTEXT) {
                const char *p;

                p = procfs_file_alloca(pid, "attr/current");
                r = read_one_line_file(p, &c->label);
                if (r < 0) {
                        if (r != -ENOENT && r != -EINVAL && r != -EPERM && r != -EACCES)
                                return r;
                } else
                        c->mask |= SD_BUS_CREDS_SELINUX_CONTEXT;
        }

        if (missing & SD_BUS_CREDS_COMM) {
                r = get_process_comm(pid, &c->comm);
                if (r < 0) {
                        if (r != -EPERM && r != -EACCES)
                                return r;
                } else
                        c->mask |= SD_BUS_CREDS_COMM;
        }

        if (missing & SD_BUS_CREDS_EXE) {
                r = get_process_exe(pid, &c->exe);
                if (r < 0) {
                        if (r != -EPERM && r != -EACCES)
                                return r;
                } else
                        c->mask |= SD_BUS_CREDS_EXE;
        }

        if (missing & SD_BUS_CREDS_CMDLINE) {
                const char *p;

                p = procfs_file_alloca(pid, "cmdline");
                r = read_full_file(p, &c->cmdline, &c->cmdline_size);
                if (r < 0) {
                        if (r == -ENOENT)
                                return -ESRCH;
                        if (r != -EPERM && r != -EACCES)
                                return r;
                } else {
                        if (c->cmdline_size == 0) {
                                free(c->cmdline);
                                c->cmdline = NULL;
                        } else
                                c->mask |= SD_BUS_CREDS_CMDLINE;
                }
        }

        if (tid > 0 && (missing & SD_BUS_CREDS_TID_COMM)) {
                _cleanup_free_ char *p = NULL;

                if (asprintf(&p, "/proc/"PID_FMT"/task/"PID_FMT"/comm", pid, tid) < 0)
                        return -ENOMEM;

                r = read_one_line_file(p, &c->tid_comm);
                if (r < 0) {
                        if (r == -ENOENT)
                                return -ESRCH;
                        if (r != -EPERM && r != -EACCES)
                                return r;
                } else
                        c->mask |= SD_BUS_CREDS_TID_COMM;
        }

        if (missing & (SD_BUS_CREDS_CGROUP|SD_BUS_CREDS_UNIT|SD_BUS_CREDS_USER_UNIT|SD_BUS_CREDS_SLICE|SD_BUS_CREDS_SESSION|SD_BUS_CREDS_OWNER_UID)) {

                r = cg_pid_get_path(NULL, pid, &c->cgroup);
                if (r < 0) {
                        if (r != -EPERM && r != -EACCES)
                                return r;
                } else {
                        r = cg_get_root_path(&c->cgroup_root);
                        if (r < 0)
                                return r;

                        c->mask |= missing & (SD_BUS_CREDS_CGROUP|SD_BUS_CREDS_UNIT|SD_BUS_CREDS_USER_UNIT|SD_BUS_CREDS_SLICE|SD_BUS_CREDS_SESSION|SD_BUS_CREDS_OWNER_UID);
                }
        }

        if (missing & SD_BUS_CREDS_AUDIT_SESSION_ID) {
                r = audit_session_from_pid(pid, &c->audit_session_id);
                if (r < 0) {
                        if (r != -ENOTSUP && r != -ENXIO && r != -ENOENT && r != -EPERM && r != -EACCES)
                                return r;
                } else
                        c->mask |= SD_BUS_CREDS_AUDIT_SESSION_ID;
        }

        if (missing & SD_BUS_CREDS_AUDIT_LOGIN_UID) {
                r = audit_loginuid_from_pid(pid, &c->audit_login_uid);
                if (r < 0) {
                        if (r != -ENOTSUP && r != -ENXIO && r != -ENOENT && r != -EPERM && r != -EACCES)
                                return r;
                } else
                        c->mask |= SD_BUS_CREDS_AUDIT_LOGIN_UID;
        }

        return 0;
}

int bus_creds_extend_by_pid(sd_bus_creds *c, uint64_t mask, sd_bus_creds **ret) {
        _cleanup_bus_creds_unref_ sd_bus_creds *n = NULL;
        int r;

        assert(c);
        assert(ret);

        if ((mask & ~c->mask) == 0 || (!(mask & SD_BUS_CREDS_AUGMENT))) {
                /* There's already all data we need, or augmentation
                 * wasn't turned on. */

                *ret = sd_bus_creds_ref(c);
                return 0;
        }

        n = bus_creds_new();
        if (!n)
                return -ENOMEM;

        /* Copy the original data over */

        if (c->mask & mask & SD_BUS_CREDS_UID) {
                n->uid = c->uid;
                n->mask |= SD_BUS_CREDS_UID;
        }

        if (c->mask & mask & SD_BUS_CREDS_EUID) {
                n->euid = c->euid;
                n->mask |= SD_BUS_CREDS_EUID;
        }

        if (c->mask & mask & SD_BUS_CREDS_SUID) {
                n->suid = c->suid;
                n->mask |= SD_BUS_CREDS_SUID;
        }

        if (c->mask & mask & SD_BUS_CREDS_FSUID) {
                n->fsuid = c->fsuid;
                n->mask |= SD_BUS_CREDS_FSUID;
        }

        if (c->mask & mask & SD_BUS_CREDS_GID) {
                n->gid = c->gid;
                n->mask |= SD_BUS_CREDS_GID;
        }

        if (c->mask & mask & SD_BUS_CREDS_EGID) {
                n->egid = c->egid;
                n->mask |= SD_BUS_CREDS_EGID;
        }

        if (c->mask & mask & SD_BUS_CREDS_SGID) {
                n->sgid = c->sgid;
                n->mask |= SD_BUS_CREDS_SGID;
        }

        if (c->mask & mask & SD_BUS_CREDS_FSGID) {
                n->fsgid = c->fsgid;
                n->mask |= SD_BUS_CREDS_FSGID;
        }

        if (c->mask & mask & SD_BUS_CREDS_SUPPLEMENTARY_GIDS) {
                n->supplementary_gids = newdup(gid_t, c->supplementary_gids, c->n_supplementary_gids);
                if (!n->supplementary_gids)
                        return -ENOMEM;
                n->n_supplementary_gids = c->n_supplementary_gids;
                n->mask |= SD_BUS_CREDS_SUPPLEMENTARY_GIDS;
        }

        if (c->mask & mask & SD_BUS_CREDS_PID) {
                n->pid = c->pid;
                n->mask |= SD_BUS_CREDS_PID;
        }

        if (c->mask & mask & SD_BUS_CREDS_TID) {
                n->tid = c->tid;
                n->mask |= SD_BUS_CREDS_TID;
        }

        if (c->mask & mask & SD_BUS_CREDS_PID_STARTTIME) {
                n->pid_starttime = c->pid_starttime;
                n->mask |= SD_BUS_CREDS_PID_STARTTIME;
        }

        if (c->mask & mask & SD_BUS_CREDS_COMM) {
                n->comm = strdup(c->comm);
                if (!n->comm)
                        return -ENOMEM;

                n->mask |= SD_BUS_CREDS_COMM;
        }

        if (c->mask & mask & SD_BUS_CREDS_TID_COMM) {
                n->tid_comm = strdup(c->tid_comm);
                if (!n->tid_comm)
                        return -ENOMEM;

                n->mask |= SD_BUS_CREDS_TID_COMM;
        }

        if (c->mask & mask & SD_BUS_CREDS_EXE) {
                n->exe = strdup(c->exe);
                if (!n->exe)
                        return -ENOMEM;

                n->mask |= SD_BUS_CREDS_EXE;
        }

        if (c->mask & mask & SD_BUS_CREDS_CMDLINE) {
                n->cmdline = memdup(c->cmdline, c->cmdline_size);
                if (!n->cmdline)
                        return -ENOMEM;

                n->cmdline_size = c->cmdline_size;
                n->mask |= SD_BUS_CREDS_CMDLINE;
        }

        if (c->mask & mask & (SD_BUS_CREDS_CGROUP|SD_BUS_CREDS_SESSION|SD_BUS_CREDS_UNIT|SD_BUS_CREDS_USER_UNIT|SD_BUS_CREDS_SLICE|SD_BUS_CREDS_OWNER_UID)) {
                n->cgroup = strdup(c->cgroup);
                if (!n->cgroup)
                        return -ENOMEM;

                n->cgroup_root = strdup(c->cgroup_root);
                if (!n->cgroup_root)
                        return -ENOMEM;

                n->mask |= mask & (SD_BUS_CREDS_CGROUP|SD_BUS_CREDS_SESSION|SD_BUS_CREDS_UNIT|SD_BUS_CREDS_USER_UNIT|SD_BUS_CREDS_SLICE|SD_BUS_CREDS_OWNER_UID);
        }

        if (c->mask & mask & (SD_BUS_CREDS_EFFECTIVE_CAPS|SD_BUS_CREDS_PERMITTED_CAPS|SD_BUS_CREDS_INHERITABLE_CAPS|SD_BUS_CREDS_BOUNDING_CAPS)) {
                n->capability = memdup(c->capability, c->capability_size);
                if (!n->capability)
                        return -ENOMEM;

                n->capability_size = c->capability_size;
                n->mask |= c->mask & mask & (SD_BUS_CREDS_EFFECTIVE_CAPS|SD_BUS_CREDS_PERMITTED_CAPS|SD_BUS_CREDS_INHERITABLE_CAPS|SD_BUS_CREDS_BOUNDING_CAPS);
        }

        if (c->mask & mask & SD_BUS_CREDS_SELINUX_CONTEXT) {
                n->label = strdup(c->label);
                if (!n->label)
                        return -ENOMEM;
                n->mask |= SD_BUS_CREDS_SELINUX_CONTEXT;
        }

        if (c->mask & mask & SD_BUS_CREDS_AUDIT_SESSION_ID) {
                n->audit_session_id = c->audit_session_id;
                n->mask |= SD_BUS_CREDS_AUDIT_SESSION_ID;
        }
        if (c->mask & mask & SD_BUS_CREDS_AUDIT_LOGIN_UID) {
                n->audit_login_uid = c->audit_login_uid;
                n->mask |= SD_BUS_CREDS_AUDIT_LOGIN_UID;
        }

        if (c->mask & mask & SD_BUS_CREDS_UNIQUE_NAME) {
                n->unique_name = strdup(c->unique_name);
                if (!n->unique_name)
                        return -ENOMEM;
                n->mask |= SD_BUS_CREDS_UNIQUE_NAME;
        }

        if (c->mask & mask & SD_BUS_CREDS_WELL_KNOWN_NAMES) {
                n->well_known_names = strv_copy(c->well_known_names);
                if (!n->well_known_names)
                        return -ENOMEM;
                n->mask |= SD_BUS_CREDS_WELL_KNOWN_NAMES;
        }

        if (c->mask & mask & SD_BUS_CREDS_DESCRIPTION) {
                n->description = strdup(c->description);
                if (!n->description)
                        return -ENOMEM;
                n->mask |= SD_BUS_CREDS_DESCRIPTION;
        }

        /* Get more data */

        r = bus_creds_add_more(n, mask,
                               c->mask & SD_BUS_CREDS_PID ? c->pid : 0,
                               c->mask & SD_BUS_CREDS_TID ? c->tid : 0);
        if (r < 0)
                return r;

        *ret = n;
        n = NULL;
        return 0;
}
