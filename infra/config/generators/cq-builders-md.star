# Copyright 2020 The Chromium Authors
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

load("//lib/try.star", "location_filters_without_defaults", "owner_whitelist_group_without_defaults", "try_")
load("//outages/config.star", outages_config = "config")

_MD_HEADER = """\
<!-- Auto-generated by lucicfg (via cq-builders-md.star). -->
<!-- Do not modify manually. -->

# List of CQ builders

[TOC]

Each builder name links to that builder on Milo.
"""

_REQUIRED_HEADER = """\
These builders must pass before a CL may land that affects files outside of
//docs and //infra/config.
"""

_OPTIONAL_HEADER = """\
These builders optionally run, depending on the files in a CL. For example, a CL
which touches `//gpu/BUILD.gn` would trigger the builder
`android_optional_gpu_tests_rel`, due to the `location_filters` values for that
builder.
"""

_EXPERIMENTAL_HEADER = """\
These builders are run on some percentage of builds. Their results are ignored
by CQ. These are often used to test new configurations before they are added
as required builders.
""" + ("""\

These builders are currently disabled due to the cq_disable_experiments outages
setting. See //infra/config/outages/README.md for more information.
""" if outages_config.disable_cq_experiments else "")

_MEGA_MODE_HEADER = """\
These builders run when the "Mega" CQ mode is triggered. This mode runs all the
builders required in the standard CQ, plus a large amount of optional builders.
"""

_TRY_BUILDER_VIEW_URL = "https://ci.chromium.org/p/{project}/builders/{bucket}/{builder}"

def _get_main_config_group_builders(ctx):
    cq_cfg = ctx.output["luci/commit-queue.cfg"]

    for c in cq_cfg.config_groups:
        if len(c.gerrit) != 1:
            continue

        gerrit = c.gerrit[0]
        if len(gerrit.projects) != 1:
            continue

        project = gerrit.projects[0]
        if len(project.ref_regexp) != 1:
            continue

        if (project.name == "chromium/src" and
            # Repeated proto fields have an internal type that won't compare equal
            # to a list, so convert it
            list(project.ref_regexp) == ["refs/heads/.+"]):
            return c.verifiers.tryjob.builders

    fail("Could not find the main CQ group")

def _normalize_builder(builder):
    location_filters = location_filters_without_defaults(builder)
    owner_whitelist_group = owner_whitelist_group_without_defaults(builder)

    return struct(
        name = builder.name,
        experiment_percentage = builder.experiment_percentage,
        includable_only = builder.includable_only,
        location_filters = location_filters,
        mode_allowlist = builder.mode_allowlist,
        equivalent_to = proto.clone(builder).equivalent_to,
        owner_whitelist_group = owner_whitelist_group,
    )

def _group_builders_by_section(builders):
    required = []
    experimental = []
    optional = []
    mega = []

    for builder in builders:
        builder = _normalize_builder(builder)
        if builder.experiment_percentage:
            experimental.append(builder)
        elif builder.location_filters:
            optional.append(builder)
        elif builder.includable_only:
            continue
        elif try_.MEGA_CQ_FULL_RUN_NAME in builder.mode_allowlist:
            mega.append(builder)
        else:
            required.append(builder)

    return struct(
        required = required,
        experimental = experimental,
        optional = optional,
        mega = mega,
    )

def _codesearch_query(url, *atoms):
    query = ["{}/search?q=".format(url)]
    for atom in atoms:
        query.append("+")
        query.append(atom)
    return "".join(query)

def _public_codesearch_query(*atoms):
    return _codesearch_query("https://cs.chromium.org", *atoms)

def _internal_codesearch_query(*atoms):
    return _codesearch_query("https://source.corp.google.com", *atoms)

def _get_location_filter_details(f):
    if f.gerrit_host_regexp != ".*" or f.gerrit_project_regexp != ".*":
        fail("cq-builders.md generator needs updating to support gerrit host/project regexp")

    regex = f.path_regexp
    title = "//" + regex.lstrip("/")
    if regex.endswith(".+"):
        regex = regex[:-len(".+")]

    url = _public_codesearch_query("file:" + regex)

    # If the regex doesn't have any interesting characters that might be part of a
    # regex, assume the regex is targeting a single path and direct link to it
    # Equals sign and dashes used by layout tests
    if all([c.isalnum() or c in "/-_=" for c in regex.codepoints()]):
        url = "https://cs.chromium.org/chromium/src/" + regex

    return struct(
        prefix = "exclude: " if f.exclude else "",
        title = title,
        url = url,
    )

def _generate_cq_builders_md(ctx):
    builders = _get_main_config_group_builders(ctx)

    builders_by_section = _group_builders_by_section(builders)

    lines = [_MD_HEADER]

    for title, header, section in (
        ("Required builders", _REQUIRED_HEADER, "required"),
        ("Optional builders", _OPTIONAL_HEADER, "optional"),
        ("Experimental builders", _EXPERIMENTAL_HEADER, "experimental"),
        ("Mega CQ builders", _MEGA_MODE_HEADER, "mega"),
    ):
        builders = getattr(builders_by_section, section)
        if not builders:
            continue

        lines.append("## %s" % title)
        lines.append(header)

        printed_projects = set()
        for b in builders:
            project, bucket, name = b.name.split("/")
            if project not in ("chrome", "chromium"):
                fail("unexpected project added to the CQ: {}".format(project))
            if project not in printed_projects:
                lines.append("### %s" % project)
                printed_projects = printed_projects.union([project])
            builder_url = _TRY_BUILDER_VIEW_URL.format(
                project = project,
                bucket = bucket,
                builder = name,
            )

            # Some builders share a common prefix (android-marshmallow-x86-rel
            # and android-marshmallow-x86-rel-non-cq for example). The quotes
            # below ensure codesearch links find the exact builder name, rather
            # than everything with a common prefix. Two sets of quotes are
            # needed because the first set is interpreted by codesearch.
            quoted_name = "\"\"{name}\"\"".format(name = name)
            if project == "chrome":
                codesearch_query = _internal_codesearch_query("file:/try/.*\\.star$")
            else:
                codesearch_query = _public_codesearch_query("file:/try/.*\\.star$")
            lines.append((
                "* [{name}]({try_builder_view}) " +
                "([definition]({definition_query}+{quoted_name}))"
            ).format(
                name = name,
                quoted_name = quoted_name,
                try_builder_view = builder_url,
                definition_query = codesearch_query,
            ))

            if b.experiment_percentage:
                lines.append("  * Experiment percentage: {percentage}".format(
                    percentage = b.experiment_percentage,
                ))

            if b.location_filters:
                lines.append("")
                lines.append("  Location filters:")
                for f in b.location_filters:
                    details = _get_location_filter_details(f)
                    lines.append("  * {prefix}[`{title}`]({url})".format(
                        prefix = details.prefix,
                        title = details.title,
                        url = details.url,
                    ))
            if b.owner_whitelist_group:
                lines.append("")
                lines.append("  This builder is only run when the CL owner is in the group:")
                for g in b.owner_whitelist_group:
                    lines.append("  * [`{group}`](https://chrome-infra-auth.appspot.com/auth/lookup?p={group})".format(group = g))

            if getattr(b, "equivalent_to") and b.equivalent_to.name:
                eq_project, eq_bucket, eq_name = b.equivalent_to.name.split("/")
                eq_builder_url = _TRY_BUILDER_VIEW_URL.format(
                    project = eq_project,
                    bucket = eq_bucket,
                    builder = eq_name,
                )
                lines.append("")
                s = ("    * Replaced with builder: [{name}]({builder}) \
when CL owner is in group \
[{group}](https://chrome-infra-auth.appspot.com/auth/lookup?p={group})".format(
                    name = eq_name,
                    builder = eq_builder_url,
                    group = b.equivalent_to.owner_whitelist_group,
                ))
                if b.equivalent_to.percentage != 100:
                    s = s + (" with percentage {p}".format(
                        p = b.equivalent_to.percentage,
                    ))
                lines.append(s)

            lines.append("")

        lines.append("")

    ctx.output["cq-builders.md"] = "\n".join(lines)

lucicfg.generator(_generate_cq_builders_md)
