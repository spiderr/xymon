#
# Xymon RPM package. Built from the xymon-monitoring/xymon tree with no
# patches at all; fixes belong upstream.
#
# This spec file is GPL-2.0-or-later; the License tag below is Xymon's,
# which the packages contain (see README.md *License*).
#
#   release   --define 'baseversion 4.3.31'
#   snapshot  ... --define 'gitsnapshot 0.202607300915git3a07523.202607301210pab12cd3'
#
# CI computes the snapshot string (see build.yml); only its leading 0.
# matters here, and that is what sorts it below the eventual -1 so
# snapshot users are absorbed when the release lands. See README.md.
#
%{!?baseversion: %global baseversion 4.3.31}
# For packaging-only re-releases of a tag: a published NEVRA is
# immutable, so a fix to a released X.Y.Z must ship as -2.
%{!?releasenum: %global releasenum 1}
%{?gitsnapshot: %global xymonrelease %{gitsnapshot}}
%{!?gitsnapshot: %global xymonrelease %{releasenum}}

# Ping helper. fping is the default: upstream's own configure prefers it
# when it finds one, and still calls its bundled xymonping "not yet fully
# stable" (build/fping.sh offers xymonping either way -- what differs is
# which the prompt defaults to). Choosing it drops the setuid bit from the
# xymonping binary, which is still shipped but inert.
#
# The cost is a repository: fping is in no RHEL repo -- not base, not CRB
# -- only EPEL, on every EL version. EL hosts therefore need EPEL enabled
# before the server package will install. Fedora carries it in base.
# --with xymonping restores the setuid helper and drops the dependency.
%bcond_with xymonping

# SELinux policy modules, built by default: the server's declares
# xymon_port_t, which %%posttrans gives to port 1984 so that the CGIs may
# reach xymond without the host-wide httpd_can_network_connect boolean.
# Nothing in CI runs enforcing, so a green build proves only that they
# compile; --without selinux leaves them out. They label no files --
# %%posttrans does that with semanage -- so there is no .fc.
%bcond_without selinux
# Not mls: the server's module requires unconfined_service_t and the
# initrc_* types, which only targeted and minimum define, so semodule
# refuses it there; and without unconfined_service_t the daemons would stay
# in init_t whatever their label. Xymon under mls needs a policy of its own.
%global selinux_variants targeted minimum

%global xymonhome  %{_prefix}/lib/xymon

# The per-role drop-ins. Named once: %%install, %%files and the %%preun
# swap guards all derive their paths from these, so a rename cannot turn
# a guard silently always-true.
%global dropindir  %{_unitdir}/xymonlaunch.service.d
%global serverconf %{dropindir}/server.conf
%global clientconf %{dropindir}/client.conf

Name:           xymon
Version:        %{baseversion}
Release:        %{xymonrelease}%{?dist}
Summary:        Xymon network and systems monitor
# Xymon's: GPL version 2, with an exemption for linking with OpenSSL, and
# a few BSD-like and public-domain files (its README, "License").
License:        GPL-2.0-only
URL:            %{?xymonurl}%{!?xymonurl:https://github.com/xymon-monitoring/xymon}
# The file rpmbuild reads is produced by the build -- a git archive of the
# ref being packaged -- so this URL is never fetched. It is here because it
# is the only line in the spec that says which upstream tree the packages
# come from, and a reader cannot tell that from a bare filename.
#
# GitHub's /archive/<ref>/<filename> form is what keeps both true at once:
# the last component is a filename hint, so the basename stays
# xymon-%%{version}.tar.gz and rpmbuild still finds the local copy, while
# the ref half names the commit or the tag.
#
# gitref is what the build was made from -- the upstream commit for a
# snapshot, the tag itself for a release -- and the build passes it because
# only the build knows: a release tag is rel-X.Y.Z (xymon's RELEASING.md),
# a snapshot names a commit, a preview its preview-X.Y.Z-N tag. Undefined,
# for the hand build in README *Building locally*, it falls back to the
# version, which is a guess -- but that build publishes nothing and its
# reader knows what they checked out.
#
# A preview's tag lives in xymon-experimental, not in xymon, so its source
# URL names that repository.
%{!?gitref: %global gitref %{version}}
%global srcrepo %{lua: if string.find(rpm.expand("%{gitref}"), "^preview%-") then print("https://github.com/xymon-monitoring/xymon-experimental") else print(rpm.expand("%{url}")) end}
Source0:        %{srcrepo}/archive/%{gitref}/%{name}-%{version}.tar.gz

# Runtime integration files; docs/upstream.md tables their provenance and why.
#   ONE unit for both roles, shipped identically by both packages;
#   xymonlaunch-run picks the role by which tree is installed:
Source1:        xymonlaunch.service
Source3:        xymonlaunch-run
#   what differs between roles, as a drop-in owned by that role's package
#   (the buildroot can hold only one file per path):
Source14:       xymonlaunch-server.conf
Source15:       xymonlaunch-client.conf
Source4:        xymonlaunch.service.preset
Source5:        xymon-tmpfiles.conf
Source6:        xymon.logrotate
Source8:        xymon-client.default
Source7:        xymonlaunch.default
Source11:       xymon-sysctl.conf
#   the policy modules, compiled in %%build; their source ships as %%doc:
Source9:        xymon.te
Source10:       xymon-client.te
#   (no bb.xml: firewalld ships the identical service, bb, on every
#   target, so a copy here was a second one of what the host has)
#   Fedora's rpm generates Requires: user(xymon) for any package owning
#   files with a non-root owner; only sysusers.d generates the Provides.
Source13:       xymon.sysusers
#   replaces upstream's clientupdate: a packaged client is updated by the
#   package manager, never by a tarball its server pushes. Policy, so no
#   upstream PR retires it; the script says why it is not simply deleted.
Source16:       clientupdate

# The client tree, shipped by BOTH packages (they conflict, so no host
# ever installs it twice). Defined once and expanded into both %%files
# sections so they cannot drift: an entry added here gets identical
# config/attr handling in either role.
#
# The config directory is globbed, as the server's is below: whatever
# upstream's client/etc holds gets packaged, rather than a list that silently
# drops what is added to it. It holds three files today; the drop-in
# directories clientlaunch.cfg and xymonclient.cfg each declare are coming,
# and an unpackaged directory is never reported -- check-files lists
# -type f -o -type l -- so a list would ship without them and say nothing.
# clientlaunch.d is the exception: the glob would ship it in both, and
# %%files xymon excludes it. Only the client package reads clientlaunch.cfg.
#
# Deliberately no xymon#NNN here: nothing upstream deletes this glob, and a
# citation in a spec comment names what would -- to a reader, and to
# tests/compensations.sh, which would ask for the glob to be deleted the day
# that pull request merged. docs/roadmap.md *The client's drop-in directories*
# carries the upstream pull request and the decision.
%global client_tree_filelist %{expand:
%{xymonhome}/client
%dir %{_sysconfdir}/xymon-client
%config(noreplace) %{_sysconfdir}/xymon-client/*
%attr(0750,root,xymon) %{xymonhome}/client/bin/logfetch
%attr(0750,root,xymon) %{xymonhome}/client/bin/clientupdate
%config(noreplace) %{_sysconfdir}/logrotate.d/xymon
%{?_sysusersdir:%{_sysusersdir}/xymon.conf}
}

# rpm will not replace a directory with a symlink -- the transaction
# fails on the existing directory -- so layout moves must happen before
# anything is unpacked. Two need it: client/etc -> /etc/xymon-client, and
# the static www trees -> %%{_datadir}/xymon/www, from www/ itself or from
# the flat %%{_datadir}/xymon of the layout before. Contents are carried over,
# so an admin's edited xymonclient.cfg or a custom gif survives; whatever
# the package ships is overwritten a moment later anyway.
%global migrate_tree %{expand:
-- mkdir -p: posix.mkdir makes one level only, and the new parents
-- (/usr/share/xymon) do not exist yet on a host still running the old
-- layout.
local function mkdirp(path)
  local acc = ""
  for part in string.gmatch(path, "[^/]+") do
    acc = acc .. "/" .. part
    posix.mkdir(acc)
  end
end

local function movetree(old, new)
  local st = posix.stat(old)
  if not (st and st.type == "directory") then return end
  mkdirp(new)
  for i, f in ipairs(posix.dir(old) or {}) do
    if f ~= "." and f ~= ".." then
      local src = old .. "/" .. f
      local dst = new .. "/" .. f
      local sst = posix.stat(src)
      if sst and sst.type == "directory" then
        movetree(src, dst)
      else
        -- Remove the source only once the copy is known to have
        -- landed: deleting on a failed write would destroy whatever
        -- the admin put there.
        local moved = posix.stat(dst) ~= nil
        if not moved then
          local fin = io.open(src, "rb")
          if fin then
            local data = fin:read("*a")
            fin:close()
            local fout = io.open(dst, "wb")
            if fout then
              fout:write(data)
              fout:close()
              moved = posix.stat(dst) ~= nil
            end
          end
        end
        if moved then os.remove(src) end
      end
    end
  end
  -- The path has to be free for the symlink either way. If anything
  -- could not be moved, keep it under .rpmorig rather than delete it.
  if not posix.rmdir(old) then
    os.rename(old, old .. ".rpmorig")
  end
end
}

# Account creation, expanded into both packages' %%pre: with no
# dependency between the packages, whichever installs first must create
# the xymon user itself. One definition so the two scriptlets cannot
# drift. (EL8 predates the sysusers rpm macros, hence the fallback.)
%global create_xymon_account %{expand:%{?sysusers_create_compat:%sysusers_create_compat %{SOURCE13}}%{!?sysusers_create_compat:
getent group xymon >/dev/null || groupadd -r xymon
getent passwd xymon >/dev/null || useradd -r -g xymon -d %{xymonhome} -s /sbin/nologin -c "Xymon monitor" xymon}
exit 0}

BuildRequires:  gcc
BuildRequires:  make
BuildRequires:  c-ares-devel
BuildRequires:  openldap-devel
BuildRequires:  openssl-devel
BuildRequires:  pcre2-devel
BuildRequires:  rrdtool-devel
BuildRequires:  libtirpc-devel
BuildRequires:  zlib-devel
BuildRequires:  systemd-rpm-macros
%{?systemd_requires}
%if %{with selinux}
BuildRequires:  checkpolicy
BuildRequires:  selinux-policy-devel
Requires(post):   policycoreutils
Requires(postun): policycoreutils
%endif

Requires(pre):  shadow-utils
%{?sysusers_requires_compat}
# A host is either a server or a client, never both. The --server build
# produces the client tree too (the server monitors itself through it),
# so this package ships it rather than depending on xymon-client, which
# makes the two mutually exclusive. Role changes are one dnf swap; the
# recipe is in README.md.
Conflicts:      xymon-client
# Until 20 Aug 2026 this package required xymon-client, so a server from
# then has both installed; every such build is 4.3.31-0.<date>. Obsoleting
# them lets dnf upgrade turn that pair into this package in one step,
# instead of stopping on the conflict above. A client-only host still
# upgrades to xymon-client: dnf prefers a same-name upgrade over an
# obsoleter, which holds only while every channel publishes both packages,
# as one spec does. Goes with the %%pretrans migration it completes.
Obsoletes:      xymon-client < 4.3.31-1
# The embedded client's collectors shell out to these, same as the
# standalone client's (see the client package's net-tools comment).
Requires:       net-tools
# This package ships /etc/logrotate.d/xymon; nothing rotates it otherwise.
Recommends:     logrotate
# Owns the apache group that rep and snap are chgrp'd to, and the
# /etc/httpd/conf.d directory the web config drops into. rpm >= 4.19
# generates a group(apache) requirement from the %%attr on its own, but
# EL8/EL9 rpm does not, and there the dirs would silently fall back to
# root's group.
Requires:       httpd-filesystem
%if %{without xymonping}
# Without the setuid xymonping helper, xymonnet shells out to fping.
Requires:       fping
%endif
# semanage, for the file-context labeling in %%posttrans. Weak on purpose: a
# container or a disabled-SELinux host works without it.
Recommends:     policycoreutils-python-utils

%description
Xymon is a system for monitoring servers, networks and applications. It
polls network services, collects data reported by clients, raises alerts,
and renders the results as a web front-end backed by RRD graphs.

This package contains the server, including the client component the
server runs on itself; it conflicts with xymon-client, which is the
same client packaged alone for every other host.

%package client
Summary:        Client for the Xymon network monitor
Requires(pre):  shadow-utils
# Both packages own the account definition: with the dependency between
# them gone, each must be able to create the xymon user on its own.
%{?sysusers_requires_compat}
%{?systemd_requires}
# xymonclient-linux.sh hardcodes ifconfig, netstat and route with no
# ip/ss fallback; without them the ifstat/ports/route sections silently
# arrive empty.
Requires:       net-tools
# The client ships /etc/logrotate.d/xymon; nothing rotates it otherwise.
Recommends:     logrotate
# semanage, for labeling client/bin in %%posttrans; weak for the server's
# reason.
Recommends:     policycoreutils-python-utils

%description client
Client-side data collection for Xymon. It gathers CPU, memory, filesystem,
process and log data from the local system and reports it to a Xymon
server, which evaluates the data against its configured thresholds.

Install it on hosts that are not the Xymon server. The server package
embeds this same client and conflicts with this package: a host is one
role or the other, and both run it as xymonlaunch.service.

%package client-local
Summary:        Local threshold analysis for the Xymon client
# Either role satisfies this: the server ships xymond_client itself and
# the standalone client needs this package to get it.
#
# The binary stays in the xymon package as well, rather than being moved
# out into this one: a server needs xymond_client for its own analysis, so
# removing it there would break every server that did not also install
# this package. Shipping it twice is the cheaper of the two.
Requires:       (xymon = %{version}-%{release} or xymon-client = %{version}-%{release})

%description client-local
Installs xymond_client on a client machine, so thresholds can be evaluated
locally instead of on the server. Configure it through localclient.cfg,
which the xymon-client package installs, and add --local to the client's
entry in clientlaunch.cfg.

%package devel
Summary:        Headers and static libraries for building Xymon modules
# A -devel package has to bring what its own headers include, or it
# builds only where Xymon's build dependencies already happen to be --
# which is a build host, not the machine of someone writing a module.
# libxymon.h reaches pcre2.h through lib/loadalerts.h; rrd.h is reached
# only by lib/rrd_api_compat.h, so that one is weak: a module that does
# not touch the RRD API does not need it.
Requires:       pcre2-devel
Recommends:     rrdtool-devel
# Convention puts static archives in a -static subpackage, so that the
# packages which linked them can be found and rebuilt when the library
# gets a security fix. That split assumes a shared library exists and
# static is the deliberate alternative; here there is none -- main's
# build has no rule that produces a .so, and XYMONLIBRARY survives only
# as a comment in build/Makefile.* that nothing reads. Splitting would
# leave -devel with headers that link against nothing and -static with
# archives that have no headers, so the archives stay here. This Provides
# records the deviation where a tool can see it, and makes a future split
# a rename rather than a break -- which is what should happen if the
# shared-library work on devel ever reaches main, at which point the .so
# belongs in xymon-libs and this package keeps the headers.
Provides:       xymon-static = %{version}-%{release}

%description devel
Headers and static libraries for building worker modules and other
components against Xymon. Xymon's own build installs neither, so this
package collects them from the build tree. Build against it with:

    cc -I%{_includedir}/xymon/include ... -L%{_libdir}/xymon -lxymon

%package tools
Summary:        Diagnostic tools for the Xymon server

%description tools
Small standalone programs for inspecting Xymon's configuration parsing and
internal structures, and for probing a network service by hand:
xymonstackio, xymonlocator, xymonavailability, xymonloadhosts and
xymonnetprobe, described in xymontools(1). Most administrators will not
need them.

%prep
%setup -q

%build
# The distribution's hardening flags, through the environment and not the
# make command line: CFLAGS= on the command line overrides the makefiles'
# `+=` and drops lib/Makefile's -I../include, while the environment is
# what build/Makefile.Linux now reads, taking CFLAGS with `?=` and
# carrying $(LDFLAGS) into every link rule (xymon#163).
#
# Before that, neither channel worked and the packages shipped Xymon's own
# -g -O2 with no FORTIFY, stack-protector or PIE. CFLAGS was assigned with
# a plain `=` and discarded while LDFLAGS survived, so exporting both
# compiled without -fPIC and linked with -pie -- which is why the build
# carried -no-pie, to undo that on Fedora and EL10, whose gcc is built
# --enable-default-pie and whose link died on an R_X86_64_32 relocation
# against non-PIC objects. Objects compiled from %%{optflags} carry -fPIE,
# so the relocation does not arise and the workaround goes with the gap it
# worked around.
export CFLAGS="%{optflags}"
# Guarded: an unconditional export would clear LDFLAGS on any target where
# the macro is undefined, which is worse than leaving it alone.
%{?build_ldflags:export LDFLAGS="%{build_ldflags}"}

# XYMONHOSTNAME is baked as "localhost" because the alternative is the
# build host's name; %%post rewrites it on first install.
#
# USERFPING is not redundant with USEXYMONPING=n: without it build/fping.sh
# probes for fping by *running* it and re-prompts on failure, and with stdin
# at EOF that loop never ends -- the build hangs instead of failing. Setting
# it skips the probe. Ignored when USEXYMONPING=y, so it needs no condition.
#
# XYMONRUNDIR (xymon#219, from 4.3.32): pidfiles and xymond_rrd's control
# sockets, in the directory xymon-tmpfiles.conf creates. Left unset, they go
# to the log directory, which the CGIs may not reach under SELinux. The
# 4.3.31 configure does not ask for it and ignores it.
USEXYMONPING=%{?with_xymonping:y}%{!?with_xymonping:n} \
USERFPING=%{_sbindir}/fping \
ENABLESSL=y \
ENABLELDAP=y \
XYMONUSER=xymon \
XYMONTOPDIR=%{xymonhome} \
XYMONVAR=%{_sharedstatedir}/xymon \
XYMONLOGDIR=%{_localstatedir}/log/xymon \
XYMONRUNDIR=/run/xymon \
XYMONHOSTNAME=localhost \
XYMONHOSTIP=127.0.0.1 \
XYMONHOSTURL=/xymon \
XYMONCGIURL=/xymon-cgi \
SECUREXYMONCGIURL=/xymon-seccgi \
CGIDIR=%{xymonhome}/cgi-bin \
SECURECGIDIR=%{xymonhome}/cgi-secure \
HTTPDGID=apache \
MANROOT=%{_mandir} \
INSTALLBINDIR=%{xymonhome}/server/bin \
INSTALLETCDIR=%{_sysconfdir}/xymon \
INSTALLWEBDIR=%{_sysconfdir}/xymon/web \
INSTALLEXTDIR=%{xymonhome}/server/ext \
INSTALLTMPDIR=%{_sharedstatedir}/xymon/tmp \
INSTALLWWWDIR=%{_sharedstatedir}/xymon/www \
INSTALLSTATICWWWDIR=%{_datadir}/xymon/www \
./configure --server

%make_build PKGBUILD=1

%if %{with selinux}
# Built in a subdirectory so the sed below does not touch the copies
# shipped as documentation. `VERSION` is a literal placeholder in both
# files and has to be substituted before checkmodule will accept them.
mkdir -p selinux-build
cp -p %{SOURCE9} %{SOURCE10} selinux-build/
sed -i 's/\bVERSION\b/%{version}/' selinux-build/*.te
for v in %{selinux_variants}; do
    make -C selinux-build -f /usr/share/selinux/devel/Makefile NAME="$v"
    mv selinux-build/xymon.pp        "selinux-build/xymon.pp.$v"
    mv selinux-build/xymon-client.pp "selinux-build/xymon-client.pp.$v"
    make -C selinux-build -f /usr/share/selinux/devel/Makefile NAME="$v" clean
done
%endif

%install
%make_install INSTALLROOT=%{buildroot} PKGBUILD=1

%if %{with selinux}
for v in %{selinux_variants}; do
    install -Dpm 0644 "selinux-build/xymon.pp.$v" \
        %{buildroot}%{_datadir}/selinux/"$v"/xymon.pp
    install -Dpm 0644 "selinux-build/xymon-client.pp.$v" \
        %{buildroot}%{_datadir}/selinux/"$v"/xymon-client.pp
done
%endif

# systemd: ONE unit for both roles, shipped by both packages (they
# conflict, so it is never on a host twice). xymonlaunch-run picks the
# role by which tree is installed.
install -Dpm 0644 %{SOURCE1}  %{buildroot}%{_unitdir}/xymonlaunch.service
install -Dpm 0755 %{SOURCE3}  %{buildroot}%{xymonhome}/client/bin/xymonlaunch-run
install -Dpm 0644 %{SOURCE14} %{buildroot}%{serverconf}
install -Dpm 0644 %{SOURCE15} %{buildroot}%{clientconf}
install -Dpm 0644 %{SOURCE4}  %{buildroot}%{_presetdir}/50-xymonlaunch.preset
install -Dpm 0644 %{SOURCE5}  %{buildroot}%{_tmpfilesdir}/xymon.conf
install -Dpm 0644 %{SOURCE6}  %{buildroot}%{_sysconfdir}/logrotate.d/xymon
install -Dpm 0644 %{SOURCE7}  %{buildroot}%{_sysconfdir}/sysconfig/xymonlaunch
install -Dpm 0644 %{SOURCE8}  %{buildroot}%{_sysconfdir}/sysconfig/xymon-client
install -Dpm 0644 %{SOURCE11} %{buildroot}%{_sysctldir}/70-xymon.conf
%if %{defined _sysusersdir}
install -Dpm 0644 %{SOURCE13} %{buildroot}%{_sysusersdir}/xymon.conf
%endif

# Over the built clientupdate, in the one client tree both packages
# ship. Its manual pages stay: the web help's index and xymon(7) link the
# HTML copy, so removing it leaves dead links, and the program says for
# itself why it declines when an update is asked for.
install -pm 0755 %{SOURCE16} %{buildroot}%{xymonhome}/client/bin/clientupdate

# Admin conveniences on $PATH, not used by the unit: xymonlaunch-run
# execs the server tree's binaries directly, so these are for people
# typing xymoncmd or xymon by hand.
install -d %{buildroot}%{_bindir} %{buildroot}%{_sbindir}
ln -sf %{xymonhome}/server/bin/xymon    %{buildroot}%{_bindir}/xymon
ln -sf %{xymonhome}/server/bin/xymoncmd %{buildroot}%{_bindir}/xymoncmd
ln -sf %{xymonhome}/server/bin/xymonlaunch %{buildroot}%{_sbindir}/xymonlaunch

# The build generates this into Xymon's own etc, which no web server
# reads; move it where httpd does. Policy, not an upstream gap: where a
# web server reads its drop-ins, and whether it loads one by itself, is
# each distribution's business (Debian stages it for dh_apache2 instead).
install -d %{buildroot}%{_sysconfdir}/httpd/conf.d
mv %{buildroot}%{_sysconfdir}/xymon/xymon-apache.conf \
   %{buildroot}%{_sysconfdir}/httpd/conf.d/xymon-apache.conf

# The www tree mixes two kinds of thing. gifs, help and menu are shipped
# content that never changes on a running host, so the FHS puts them in
# /usr/share; html, notes, rep, snap and wml are state, and xymongen
# writes generated pages into www itself, so XYMONWWWDIR stays writable
# where it is.
#
# INSTALLSTATICWWWDIR (configured above, xymon#414, from 4.3.32) makes the
# build install the static three into /usr/share/xymon/www and symlink them back
# into the www tree, so every /xymon/ URL and on-disk path still works and
# nothing in xymonserver.cfg or the CGIs learns a second location. httpd
# needs no extra config: the generated one sets FollowSymLinks on the www
# directory, which is what lets it cross into /usr/share (verified -- without
# it the same request is a 403).
# 4.3.31's configure ignores INSTALLSTATICWWWDIR, and the stable channel
# still builds it: there the loop below does the move by hand. Where the
# build already made the link it does nothing; it goes once the stable
# channel builds 4.3.32.
install -d %{buildroot}%{_datadir}/xymon/www
for d in gifs help menu; do
    [ -L %{buildroot}%{_sharedstatedir}/xymon/www/$d ] && continue
    mv %{buildroot}%{_sharedstatedir}/xymon/www/$d %{buildroot}%{_datadir}/xymon/www/$d
    ln -sf %{_datadir}/xymon/www/$d %{buildroot}%{_sharedstatedir}/xymon/www/$d
done

# rep and snap, the on-demand reports and snapshots, are the third kind:
# regenerable, safe to delete, so the FHS puts them in /var/cache. Moved
# and symlinked back like the static three, so the URLs and any
# xymonserver.cfg kept from an older install still land there. From 4.3.32
# XYMONCACHEWWWDIR (xymon#443) names the new place, so the shipped config
# points at it directly; 4.3.31's has no such line and the sed changes
# nothing. xymon-tmpfiles.conf recreates the directories if /var/cache is
# cleared: snapshot.cgi does not make its own (xymon#443).
install -d %{buildroot}%{_localstatedir}/cache/xymon/www
for d in rep snap; do
    mv %{buildroot}%{_sharedstatedir}/xymon/www/$d %{buildroot}%{_localstatedir}/cache/xymon/www/$d
    ln -sf %{_localstatedir}/cache/xymon/www/$d %{buildroot}%{_sharedstatedir}/xymon/www/$d
done
sed -i 's|^XYMONCACHEWWWDIR="[^"]*"|XYMONCACHEWWWDIR="%{_localstatedir}/cache/xymon/www"|' \
    %{buildroot}%{_sysconfdir}/xymon/xymonserver.cfg

# That config points AuthUserFile/AuthGroupFile at these; the build never
# creates them, and without the password file every /xymon-seccgi request
# is a 500. Shipped empty: no user can authenticate until the admin adds
# one with htpasswd.
touch %{buildroot}%{_sysconfdir}/xymon/xymonpasswd
touch %{buildroot}%{_sysconfdir}/xymon/xymongroups

# The modules' source, shipped as %%doc from the build dir --
# %%doc cannot take a %%{SOURCEn} path directly.
cp -p %{SOURCE9} %{SOURCE10} .

# Development files. The build installs neither headers nor the static
# libraries, so take them from the build tree. include/ and lib/ must stay
# siblings: libxymon.h pulls in 52 further headers as "../lib/...", so a
# flat include directory would break on the first #include.
install -d %{buildroot}%{_includedir}/xymon/include \
           %{buildroot}%{_includedir}/xymon/lib \
           %{buildroot}%{_libdir}/xymon
install -pm 0644 include/*.h %{buildroot}%{_includedir}/xymon/include/
install -pm 0644 lib/*.h     %{buildroot}%{_includedir}/xymon/lib/
install -pm 0644 lib/*.a     %{buildroot}%{_libdir}/xymon/

# xymon.pc: ship a pkg-config file with -devel, written by hand. The one
# install-devel generates (xymon#410) is close but not the same: its prefix is
# Xymon's home, unused since includedir and libdir are absolute, and its
# Libs.private has no -lz, which nothing in lib/ needs. Shipping it instead
# would drop -lz here.
mkdir -p %{buildroot}%{_libdir}/pkgconfig
cat > %{buildroot}%{_libdir}/pkgconfig/xymon.pc <<'PC'
prefix=%{_prefix}
includedir=%{_includedir}/xymon
libdir=%{_libdir}/xymon

Name: xymon
Description: Xymon monitoring system libraries and headers
Version: %{version}
Cflags: -I${includedir}/include
Libs: -L${libdir} -lxymon -lxymoncomm -lxymontime
Libs.private: -lpcre2-8 -lssl -lcrypto -lz -ltirpc -lrrd
PC

# Diagnostic tools. The build makes them in its `all` target, but only the
# opt-in install-tools target installs them, so %%make_install above does
# not. Through that target rather than copied by name, so xymon-tools ships
# exactly what upstream installs: not tree, which lib/ builds but does not
# install. It takes INSTALLBINDIR and MANROOT from the configure step.
%{__make} install-tools INSTALLROOT=%{buildroot} PKGBUILD=1

# The client tree ships its own tmp/logs as real dirs; redirect them.
rm -rf %{buildroot}%{xymonhome}/client/tmp %{buildroot}%{xymonhome}/client/logs
ln -sf %{_tmppath}            %{buildroot}%{xymonhome}/client/tmp
ln -sf %{_localstatedir}/log/xymon %{buildroot}%{xymonhome}/client/logs

# Client configuration belongs in /etc: the FHS wants /usr shareable and
# read-only, and xymonclient.cfg (XYMSRV) is the most-edited file on a
# client host. Upstream's build does this for the server -- server/etc ->
# INSTALLETCDIR -- but offers no client equivalent until xymon#411, so
# the same move is made here. Every reference to these files, in
# clientlaunch.cfg and the server's tasks.cfg alike, is spelled
# $XYMONCLIENTHOME/etc/... and resolves through the symlink.
install -d %{buildroot}%{_sysconfdir}/xymon-client
mv %{buildroot}%{xymonhome}/client/etc/* %{buildroot}%{_sysconfdir}/xymon-client/

# The drop-in directories clientlaunch.cfg and xymonclient.cfg each declare,
# which nothing creates: xymond/Makefile makes all nine of the server's, and
# client/Makefile makes neither of these, until xymon#411. They are empty --
# the point is the directory, not its contents. Without it the only way to
# add a client task is to edit clientlaunch.cfg, which is %%config(noreplace):
# an edited copy stops following upstream, and the day the shipped file does
# change it arrives as a .rpmnew to merge by hand rather than as an upgrade.
# With the directory, a customisation is a file of the admin's own and the
# shipped config keeps upgrading. The `optional` in the declaration means a
# missing directory is skipped silently (lib/stackio.c, dbgprintf rather than
# errprintf), so nothing says why a drop-in was never read.
#
# Both are mkdir'd here because there is one buildroot. xymonclient.d ships
# in both packages -- the server's embedded client still reads
# xymonclient.cfg. clientlaunch.d ships only in the client package: the
# server never reads clientlaunch.cfg (it launches the embedded client from
# tasks.cfg), so a drop-in there would be silently unused; extra server
# tasks belong in tasks.d. The %%files split is next to each listing.
install -d %{buildroot}%{_sysconfdir}/xymon-client/clientlaunch.d \
           %{buildroot}%{_sysconfdir}/xymon-client/xymonclient.d
rmdir %{buildroot}%{xymonhome}/client/etc
ln -sf %{_sysconfdir}/xymon-client %{buildroot}%{xymonhome}/client/etc

%pretrans -p <lua>
%migrate_tree
movetree("%{xymonhome}/client/etc", "%{_sysconfdir}/xymon-client")
movetree("%{_sharedstatedir}/xymon/www/gifs", "%{_datadir}/xymon/www/gifs")
movetree("%{_sharedstatedir}/xymon/www/help", "%{_datadir}/xymon/www/help")
movetree("%{_sharedstatedir}/xymon/www/menu", "%{_datadir}/xymon/www/menu")
-- The flat layout before it, %{_datadir}/xymon/{gifs,help,menu}: what an
-- admin added there follows into www/.
movetree("%{_datadir}/xymon/gifs", "%{_datadir}/xymon/www/gifs")
movetree("%{_datadir}/xymon/help", "%{_datadir}/xymon/www/help")
movetree("%{_datadir}/xymon/menu", "%{_datadir}/xymon/www/menu")
movetree("%{_sharedstatedir}/xymon/www/rep", "%{_localstatedir}/cache/xymon/www/rep")
movetree("%{_sharedstatedir}/xymon/www/snap", "%{_localstatedir}/cache/xymon/www/snap")

%pretrans client -p <lua>
-- The client package ships no www tree, so it has only the one move.
%migrate_tree
movetree("%{xymonhome}/client/etc", "%{_sysconfdir}/xymon-client")

%pre
%create_xymon_account

%pre client
%create_xymon_account

%post
# Skipped during a promotion, for the reason given in %%post client: the
# departing package's state must not be overridden by a preset run.
if [ ! -e %{clientconf} ]; then
%systemd_post xymonlaunch.service
fi
%tmpfiles_create %{_tmpfilesdir}/xymon.conf
# Replace the placeholder baked in at build time (see %%build).
#
# hosts.cfg gets the same treatment, and it is not cosmetic. The build
# bakes "localhost" there too, but the server's own client reports under
# uname -n, and xymond_client silently drops a report whose host it
# cannot find: it logs "Client report from host X" and returns before
# running a single check, so cpu, disk, memory and procs never appear
# while the host still shows green from the network tests. Adding a
# second entry does not fix it either -- Xymon keys hosts by address, so
# another 127.0.0.1 line is ignored. The entry has to be renamed.
if [ $1 -eq 1 ]; then
    sed -i -e "s/^XYMONSERVERHOSTNAME=.*/XYMONSERVERHOSTNAME=\"$(uname -n)\"/" \
           -e "s/^XYMONSERVERWWWNAME=.*/XYMONSERVERWWWNAME=\"$(uname -n)\"/" \
        %{_sysconfdir}/xymon/xymonserver.cfg || :
    sed -i -e "s/^\\(127\\.0\\.0\\.1[[:space:]]\\+\\)localhost\\b/\\1$(uname -n)/" \
        %{_sysconfdir}/xymon/hosts.cfg || :
fi
%if %{with selinux}
# Never fatal: a container or a machine with SELinux disabled has no
# working semodule, and failing there would abort an otherwise fine
# installation. Before %%posttrans labels port 1984 with the module's
# xymon_port_t.
for v in %{selinux_variants}; do
    semodule -s "$v" -i %{_datadir}/selinux/"$v"/xymon.pp >/dev/null 2>&1 || :
done
%endif

%posttrans
# In %%posttrans, not %%post: semanage comes from a weak dependency, which
# does not order the transaction -- on Fedora 44 dnf installed it after
# this package, %%post found no semanage, and nothing was labeled. By now
# everything the transaction brings is in place.
# Label the tree so it works on an enforcing host: the default policy has
# no contexts for /usr/lib/xymon, so everything there is lib_t and the
# state under /var/lib/xymon var_lib_t.
#  - bin/ as bin_t: systemd has no transition for a lib_t ExecStart, so
#    xymonlaunch and every daemon it starts would stay in init_t, which
#    may not reach xymond's port, the client's temp files or the network
#    tests' ports. From bin_t they run as unconfined_service_t.
#  - cgiwrap back to httpd_sys_script_exec_t, after bin/: every CGI in
#    cgi-bin and cgi-secure is a hard link to it, one inode, so bin_t
#    there would relabel all of them and they would run as httpd_t.
#  - rrd/ readable, and tmp/ and /run/xymon writable, by the CGIs: showgraph
#    reads the RRD files and writes xymond_rrd's rrdctl sockets to flush
#    them -- in tmp/ up to 4.3.31, in XYMONRUNDIR from 4.3.32.
#  - port 1984 as xymon_port_t, which the module lets only the CGIs reach.
# /run/xymon is named /var/run/xymon: the policy makes /run equivalent to
# /var/run, and semanage refuses a rule under /run ("conflicts with
# equivalency rule"); the label then applies to /run/xymon, as matchpathcon
# shows.
# Best-effort by design -- semanage only exists where the SELinux tooling
# is installed, and a container or disabled-SELinux host needs none of
# this. An upgrade re-adds rules it already has, which semanage refuses.
if command -v semanage >/dev/null 2>&1; then
    semanage fcontext -a -t httpd_sys_script_exec_t '%{xymonhome}/cgi-bin(/.*)?'    2>/dev/null || :
    semanage fcontext -a -t httpd_sys_script_exec_t '%{xymonhome}/cgi-secure(/.*)?' 2>/dev/null || :
    semanage fcontext -a -t httpd_sys_content_t '%{_sharedstatedir}/xymon/www(/.*)?' 2>/dev/null || :
    semanage fcontext -a -t httpd_sys_content_t '%{_datadir}/xymon(/.*)?' 2>/dev/null || :
    # rep and snap moved to /var/cache; the rules an older package added
    # for their /var/lib paths would otherwise outlive it.
    semanage fcontext -d '%{_sharedstatedir}/xymon/www/rep(/.*)?'  2>/dev/null || :
    semanage fcontext -d '%{_sharedstatedir}/xymon/www/snap(/.*)?' 2>/dev/null || :
    semanage fcontext -a -t httpd_sys_content_t '%{_localstatedir}/cache/xymon(/.*)?' 2>/dev/null || :
    semanage fcontext -a -t httpd_sys_rw_content_t '%{_localstatedir}/cache/xymon/www/rep(/.*)?'  2>/dev/null || :
    semanage fcontext -a -t httpd_sys_rw_content_t '%{_localstatedir}/cache/xymon/www/snap(/.*)?' 2>/dev/null || :
    semanage fcontext -a -t bin_t '%{xymonhome}/server/bin(/.*)?' 2>/dev/null || :
    semanage fcontext -a -t bin_t '%{xymonhome}/client/bin(/.*)?' 2>/dev/null || :
    semanage fcontext -a -t httpd_sys_script_exec_t '%{xymonhome}/server/bin/cgiwrap' 2>/dev/null || :
    semanage fcontext -a -t httpd_sys_content_t '%{_sharedstatedir}/xymon/rrd(/.*)?'    2>/dev/null || :
    semanage fcontext -a -t httpd_sys_rw_content_t '%{_sharedstatedir}/xymon/tmp(/.*)?' 2>/dev/null || :
    semanage fcontext -a -t httpd_sys_rw_content_t '/var/run/xymon(/.*)?' 2>/dev/null || :
    restorecon -R %{xymonhome}/cgi-bin %{xymonhome}/cgi-secure \
        %{xymonhome}/server/bin %{xymonhome}/client/bin \
        %{_sharedstatedir}/xymon/www %{_sharedstatedir}/xymon/rrd \
        %{_sharedstatedir}/xymon/tmp %{_datadir}/xymon /run/xymon \
        %{_localstatedir}/cache/xymon 2>/dev/null || :
%if %{with selinux}
    semanage port -a -t xymon_port_t -p tcp 1984 2>/dev/null || :
%endif
fi
# A running httpd does not pick up a new conf.d drop-in until it reloads,
# so without this the /xymon UI stays unreachable until the next restart.
# Policy, not an upstream gap: the service name and the init system are
# distribution business. Best-effort because this package requires
# httpd-filesystem, not httpd -- a host without a web server, or a
# container with no PID 1, is fine. try-reload-or-restart reloads if
# httpd is up and does not start it if it is not; a failed reload must
# not abort the install. After the labeling above, so httpd can reach
# the trees it has just been pointed at.
if [ -d /run/systemd/system ]; then
    systemctl try-reload-or-restart httpd.service >/dev/null 2>&1 || :
fi

%preun
# Swap-aware: in a demotion (dnf swap xymon xymon-client) the client
# package is already installed when this runs -- installs precede erases
# -- so enablement must survive. Its packaged drop-in is the marker: it
# lives under /usr/lib (admin drop-ins go to /etc/systemd), so it cannot
# be faked by configuration, and unlike `rpm -q` it cannot deadlock the
# rpmdb from inside a scriptlet.
#
# The server IS stopped here, while server.conf still applies: that is
# what keeps xymond's children alive through the stop so they can finish
# checkpoints. Stopping after the erase removed the drop-in would kill
# the cgroup mid-write. Enablement is untouched, so the swap's
# `systemctl restart` brings the new role up.
if [ -e %{clientconf} ]; then
    systemctl stop xymonlaunch.service >/dev/null 2>&1 || :
else
%systemd_preun xymonlaunch.service
fi

%postun
%systemd_postun_with_restart xymonlaunch.service
# The removal half of the %%posttrans reload. The conf.d drop-in is gone, but a
# running httpd keeps serving /xymon from the config it last read, aliased
# to trees just deleted, until something reloads it. Erase only: on an
# upgrade the new package's %%posttrans reloads against the new
# file. Same guard and same best effort as %post.
if [ $1 -eq 0 ] && [ -d /run/systemd/system ]; then
    systemctl try-reload-or-restart httpd.service >/dev/null 2>&1 || :
fi
if [ $1 -eq 0 ] && command -v semanage >/dev/null 2>&1; then
    semanage fcontext -d '%{xymonhome}/cgi-bin(/.*)?'    2>/dev/null || :
    semanage fcontext -d '%{xymonhome}/cgi-secure(/.*)?' 2>/dev/null || :
    semanage fcontext -d '%{_sharedstatedir}/xymon/www(/.*)?'      2>/dev/null || :
    semanage fcontext -d '%{_datadir}/xymon(/.*)?'                 2>/dev/null || :
    semanage fcontext -d '%{_localstatedir}/cache/xymon(/.*)?'          2>/dev/null || :
    semanage fcontext -d '%{_localstatedir}/cache/xymon/www/rep(/.*)?'  2>/dev/null || :
    semanage fcontext -d '%{_localstatedir}/cache/xymon/www/snap(/.*)?' 2>/dev/null || :
    semanage fcontext -d '%{xymonhome}/server/bin(/.*)?'           2>/dev/null || :
    semanage fcontext -d '%{xymonhome}/server/bin/cgiwrap'         2>/dev/null || :
    semanage fcontext -d '%{_sharedstatedir}/xymon/rrd(/.*)?'      2>/dev/null || :
    semanage fcontext -d '%{_sharedstatedir}/xymon/tmp(/.*)?'      2>/dev/null || :
    semanage fcontext -d '/var/run/xymon(/.*)?'                     2>/dev/null || :
    # Shared with the client package: in a demotion it is installed by
    # now (see %%preun), and its client/bin still needs the rule.
    if [ ! -e %{clientconf} ]; then
        semanage fcontext -d '%{xymonhome}/client/bin(/.*)?'       2>/dev/null || :
    fi
    # Before the module goes: semodule will not remove a type in use.
    semanage port -d -t xymon_port_t -p tcp 1984 2>/dev/null || :
fi
%if %{with selinux}
if [ $1 -eq 0 ]; then
    for v in %{selinux_variants}; do
        semodule -s "$v" -r xymon >/dev/null 2>&1 || :
    done
fi
%endif

%files
%license COPYING
%doc README README.CLIENT RELEASENOTES CREDITS Changes
%{_mandir}/man*/*
%exclude %{_mandir}/man1/xymontools.1*
%{_bindir}/xymon
%{_bindir}/xymoncmd
%{_sbindir}/xymonlaunch
%{_unitdir}/xymonlaunch.service
%dir %{dropindir}
%{serverconf}
%{_presetdir}/50-xymonlaunch.preset
%{_tmpfilesdir}/xymon.conf
%{_sysctldir}/70-xymon.conf
%config(noreplace) %{_sysconfdir}/sysconfig/xymonlaunch
%config(noreplace) %{_sysconfdir}/httpd/conf.d/xymon-apache.conf
%dir %{_sysconfdir}/xymon
%dir %{_sysconfdir}/xymon/tasks.d
%dir %{_sysconfdir}/xymon/web
%config(noreplace) %{_sysconfdir}/xymon/*
# Apache-writable overrides of the glob above (the later entry wins, as
# with logfetch below): the critical-editor CGI writes critical.cfg, and
# httpd reads the auth files the shipped apache config points at.
%config(noreplace) %attr(0644,apache,apache) %{_sysconfdir}/xymon/critical.cfg
%config(noreplace) %attr(0644,apache,apache) %{_sysconfdir}/xymon/critical.cfg.bak
%config(noreplace) %attr(0600,apache,apache) %{_sysconfdir}/xymon/xymonpasswd
%config(noreplace) %attr(0600,apache,apache) %{_sysconfdir}/xymon/xymongroups
%dir %{xymonhome}
%{xymonhome}/server
%exclude %{xymonhome}/server/bin/xymonstackio
%exclude %{xymonhome}/server/bin/xymonlocator
%exclude %{xymonhome}/server/bin/xymonavailability
%exclude %{xymonhome}/server/bin/xymonloadhosts
%exclude %{xymonhome}/server/bin/xymonnetprobe
%{xymonhome}/cgi-bin
%{xymonhome}/cgi-secure
# Static web content -- gifs, help, menu -- in %%{_datadir}/xymon/www,
# reached through symlinks in the www tree (see %%install).
%{_datadir}/xymon
# The embedded client: the same tree the build produces for the
# standalone package, shipped here because the server runs it on itself
# through tasks.cfg. One shared definition with %%files client (see the
# %%global at the top), so the two roles cannot drift.
%client_tree_filelist
# The glob would otherwise ship clientlaunch.d here too. The server never
# reads clientlaunch.cfg -- extra tasks belong in tasks.d -- so owning
# the directory would invite drop-ins that are silently never loaded.
%exclude %{_sysconfdir}/xymon-client/clientlaunch.d
%attr(0755,xymon,xymon) %dir %{_localstatedir}/log/xymon
%attr(-,xymon,xymon) %{_sharedstatedir}/xymon
%attr(0755,xymon,xymon) %dir %{_localstatedir}/cache/xymon
%attr(0755,xymon,xymon) %dir %{_localstatedir}/cache/xymon/www
%attr(0775,xymon,apache) %dir %{_localstatedir}/cache/xymon/www/rep
%attr(0775,xymon,apache) %dir %{_localstatedir}/cache/xymon/www/snap
%if %{with xymonping}
%attr(4750,root,xymon) %{xymonhome}/server/bin/xymonping
%endif
%if %{with selinux}
%{_datadir}/selinux/*/xymon.pp
%endif
# The modules' source, beside the compiled ones %%post loads.
%doc xymon.te
%doc xymon-client.te

%post client
# In a demotion the server package -- and its "enable xymonlaunch"
# preset -- is still installed here. Running the preset would re-enable
# a unit the admin may have disabled, and contradict this package's
# no-preset design (see %%files client). The %%preun guards already carry
# enablement across a swap.
if [ ! -e %{serverconf} ]; then
%systemd_post xymonlaunch.service
fi
# The old layout shipped xymon-client.service; carry its enablement and
# any running instance over to the shared name, then clear it.
# is-enabled and is-active are separate checks: an enabled-but-stopped
# client must stay enabled. Gated on the old unit file still existing,
# true only during the migrating upgrade -- otherwise this would run on
# every future upgrade and stop an admin's own unit of that name.
if [ $1 -ge 2 ] && [ -e %{_unitdir}/xymon-client.service ]; then
    if systemctl is-enabled --quiet xymon-client.service 2>/dev/null; then
        systemctl --no-reload enable xymonlaunch.service >/dev/null 2>&1 || :
    fi
    if systemctl is-active --quiet xymon-client.service 2>/dev/null; then
        systemctl stop xymon-client.service >/dev/null 2>&1 || :
        systemctl start xymonlaunch.service >/dev/null 2>&1 || :
    fi
    systemctl --no-reload disable xymon-client.service >/dev/null 2>&1 || :
fi
%if %{with selinux}
for v in %{selinux_variants}; do
    semodule -s "$v" -i %{_datadir}/selinux/"$v"/xymon-client.pp >/dev/null 2>&1 || :
done
%endif
exit 0

%posttrans client
# client/bin as bin_t, for the reason given in the server's %%posttrans:
# from a lib_t ExecStart the client would run in init_t. In %%posttrans
# for the same reason too, and with the same best effort.
if command -v semanage >/dev/null 2>&1; then
    semanage fcontext -a -t bin_t '%{xymonhome}/client/bin(/.*)?' 2>/dev/null || :
    restorecon -R %{xymonhome}/client/bin 2>/dev/null || :
fi

%preun client
# Mirror of the server's guard: in a promotion (dnf swap xymon-client
# xymon) the server package is already installed when this runs, the
# unit's enablement must survive the client package's erase, and the
# running client is stopped here while client.conf still applies -- its
# cgroup kill is what stops the report script with it.
if [ -e %{serverconf} ]; then
    systemctl stop xymonlaunch.service >/dev/null 2>&1 || :
else
%systemd_preun xymonlaunch.service
fi

%postun client
%systemd_postun_with_restart xymonlaunch.service
# Kept in a promotion: the server package is installed by now (see
# %%preun client) and labels the same client/bin.
if [ $1 -eq 0 ] && [ ! -e %{serverconf} ] && command -v semanage >/dev/null 2>&1; then
    semanage fcontext -d '%{xymonhome}/client/bin(/.*)?' 2>/dev/null || :
fi
%if %{with selinux}
if [ $1 -eq 0 ]; then
    for v in %{selinux_variants}; do
        semodule -s "$v" -r xymon-client >/dev/null 2>&1 || :
    done
fi
%endif
exit 0

%files client
%license COPYING
%doc README.CLIENT
# The same unit the server package ships -- one service name,
# xymonlaunch.service, on every host; xymonlaunch-run picks the role --
# plus the client role's drop-in. Deliberately NO preset: a fresh
# client must not start unconfigured against the baked-in 127.0.0.1,
# so enablement stays an explicit admin step.
%{_unitdir}/xymonlaunch.service
%dir %{dropindir}
%{clientconf}
%config(noreplace) %{_sysconfdir}/sysconfig/xymon-client
%dir %{xymonhome}
%client_tree_filelist
# Packaged here only: see the matching %%exclude in %%files. Named
# explicitly so a glob-behaviour change cannot drop it from the one
# package that needs it.
%dir %{_sysconfdir}/xymon-client/clientlaunch.d
%attr(0755,xymon,xymon) %dir %{_localstatedir}/log/xymon
%if %{with selinux}
%{_datadir}/selinux/*/xymon-client.pp
%endif

%files client-local
%license COPYING
%{xymonhome}/server/bin/xymond_client
%{_mandir}/man8/xymond_client.8*
%{_mandir}/man5/analysis.cfg.5*

%files devel
%license COPYING
%dir %{_includedir}/xymon
%{_includedir}/xymon/include
%{_includedir}/xymon/lib
%dir %{_libdir}/xymon
%{_libdir}/xymon/*.a
%{_libdir}/pkgconfig/xymon.pc

%files tools
%license COPYING
%{xymonhome}/server/bin/xymonstackio
%{xymonhome}/server/bin/xymonlocator
%{xymonhome}/server/bin/xymonavailability
%{xymonhome}/server/bin/xymonloadhosts
%{xymonhome}/server/bin/xymonnetprobe
%{_mandir}/man1/xymontools.1*

%changelog
