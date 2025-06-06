// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::config;
use crate::group::Group;

use guppy::graph::PackageGraph;
use guppy::PackageId;

fn get_group(id: &PackageId, graph: &PackageGraph, config: &config::BuildConfig) -> Option<Group> {
    let name = graph.metadata(id).unwrap().name();
    config.per_crate_config.get(name)?.group
}

// TODO(https://crbug.com/395924069): Delete this functiona and use `collect_dependencies` result
// instead.  This will be possible once `gnrt vendor` is also transitioned to
// use `guppy` and `collect_dependencies` instead of working directly with
// `cargo_metadata`.
pub fn find_inherited_privilege_group(
    id: &PackageId,
    root: &PackageId,
    graph: &PackageGraph,
    config: &config::BuildConfig,
) -> Group {
    // A group is inherited from its ancestors and its dependencies, including
    // from itself.
    // - It inherits the highest privilege of any ancestor. If everything only uses
    //   it in the sandbox, then it only needs to be in the sandbox. Same for tests.
    // - It inherits the lowest privilege of any dependency. If a dependency that is
    //   part of it needs a sandbox, then so does it.
    // - If the group is specified on the crate itself, it replaces all ancestors.
    let mut ancestor_groups = Vec::<Group>::new();
    let mut dependency_groups = Vec::<Group>::new();

    for package in graph.packages() {
        let each_id = package.id();
        let found_group = get_group(each_id, graph, config).or_else(|| {
            if graph.directly_depends_on(root, each_id).unwrap() {
                // If the dependency is a top-level dep of Chromium, then it defaults to this
                // privilege level.
                // TODO: Default should be sandbox??
                Some(Group::Safe)
            } else {
                None
            }
        });

        if let Some(group) = found_group {
            if graph.depends_on(each_id, id).unwrap() {
                // `each_id` is an ancestor of `id`, or is the same crate.
                log::debug!("{id} ancestor {each_id} ({group:?})");
                ancestor_groups.push(group);
            } else if graph.depends_on(id, each_id).unwrap() {
                // `each_id` is an descendent of `id`.
                log::debug!("{id} dependency {each_id} ({group:?})");
                dependency_groups.push(group);
            }
        };
    }

    if let Some(self_group) = get_group(id, graph, config) {
        ancestor_groups.clear();
        ancestor_groups.push(self_group);
    }

    // Combine the privileges together. Ancestors work to increase privilege,
    // and dependencies work to decrease it.
    let ancestor_privilege = ancestor_groups.into_iter().fold(Group::Test, std::cmp::max);
    let depedency_privilege = dependency_groups.into_iter().fold(Group::Safe, std::cmp::min);
    let privilege = std::cmp::min(ancestor_privilege, depedency_privilege);
    log::debug!("privilege = {privilege:?}");
    privilege
}

/// Finds the value of a config flag for a crate that is inherited from
/// ancestors. The inherited value will be true if its true for the crate
/// itself or for any ancestor.
///
/// The `get_flag` function retrieves the flag value (if any is set) for
/// each crate.
///
/// If the crate (or an ancestor crate) is a top-level dependency and does not
/// have a value for its flag defined by `get_flag`, the
/// `get_flag_for_top_level` function defines its value based on its
/// [`Group`].
fn find_inherited_bool_flag(
    id: &PackageId,
    root: &PackageId,
    graph: &PackageGraph,
    config: &config::BuildConfig,
    mut get_flag: impl FnMut(&PackageId) -> Option<bool>,
    mut get_flag_for_top_level: impl FnMut(Option<Group>) -> Option<bool>,
) -> Option<bool> {
    let mut inherited_flag = None;

    for package in graph.packages() {
        let each_id = package.id();
        let group = get_group(each_id, graph, config);

        if let Some(flag) = get_flag(each_id).or_else(|| {
            if graph.directly_depends_on(root, each_id).unwrap() {
                get_flag_for_top_level(group)
            } else {
                None
            }
        }) {
            if graph.depends_on(each_id, id).unwrap() {
                log::debug!("{id} ancestor {each_id} ({flag:?})");
                inherited_flag = Some(inherited_flag.unwrap_or_default() || flag);
            }
        };
    }
    inherited_flag
}

/// Finds the security_critical flag to be used for a package `id`.
///
/// A package is considered security_critical if any ancestor is explicitly
/// marked security_critical. If the package and ancestors do not specify it,
/// then this function returns None.
pub fn find_inherited_security_critical_flag(
    id: &PackageId,
    root: &PackageId,
    graph: &PackageGraph,
    config: &config::BuildConfig,
) -> Option<bool> {
    let get_security_critical = |id: &PackageId| {
        let name = graph.metadata(id).unwrap().name();
        config.per_crate_config.get(name).and_then(|config| config.security_critical)
    };
    let get_top_level_security_critical = |group: Option<Group>| {
        // If the dependency is a top-level dep of Chromium and is not put into the test
        // group, then it defaults to security_critical.
        match group {
            Some(Group::Safe) | Some(Group::Sandbox) | None => Some(true),
            Some(Group::Test) => None,
        }
    };

    let inherited_flag = find_inherited_bool_flag(
        id,
        root,
        graph,
        config,
        get_security_critical,
        get_top_level_security_critical,
    );
    log::debug!("{id} security_critical {inherited_flag:?}");
    inherited_flag
}

/// Finds the shipped flag to be used for a package `id`.
///
/// A package is considered shipped if any ancestor is explicitly marked
/// shipped. If the package and ancestors do not specify it, then this
/// function returns None.
pub fn find_inherited_shipped_flag(
    id: &PackageId,
    root: &PackageId,
    graph: &PackageGraph,
    config: &config::BuildConfig,
) -> Option<bool> {
    let get_shipped = |id: &PackageId| {
        let name = graph.metadata(id).unwrap().name();
        config.per_crate_config.get(name).and_then(|config| config.shipped)
    };
    let get_top_level_shipped = |group: Option<Group>| {
        // If the dependency is a top-level dep of Chromium and is not put into the test
        // group, then it defaults to shipped.
        match group {
            Some(Group::Safe) | Some(Group::Sandbox) | None => Some(true),
            Some(Group::Test) => None,
        }
    };

    let inherited_flag =
        find_inherited_bool_flag(id, root, graph, config, get_shipped, get_top_level_shipped);
    log::debug!("{id} shipped {inherited_flag:?}");
    inherited_flag
}
