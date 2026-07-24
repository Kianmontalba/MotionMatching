#!/usr/bin/env python
#
# Build entry point for the Godot Motion Matching GDExtension.
#
#   scons platform=windows target=template_debug
#   scons platform=linux    target=template_release
#   scons platform=macos    target=template_release arch=universal
#   scons platform=android  target=template_release arch=arm64
#
# godot-cpp is expected in godot-cpp/ (git submodule).

import os
import sys

env = SConscript("godot-cpp/SConstruct")

# C++20 is required by the framework.
if env["platform"] == "windows" and env.get("is_msvc", False):
    env.Append(CXXFLAGS=["/std:c++20"])
else:
    env.Append(CXXFLAGS=["-std=c++20"])

env.Append(CPPPATH=["include/", "src/", "editor/"])

sources = Glob("src/*.cpp")

# Editor tooling is compiled only into editor builds.
if env["target"] in ("editor", "template_debug"):
    env.Append(CPPDEFINES=["TOOLS_ENABLED"])
    sources += Glob("editor/*.cpp")

# Release builds get the aggressive vectorization the search loop benefits from.
if env["target"] == "template_release":
    if env["platform"] == "windows" and env.get("is_msvc", False):
        env.Append(CXXFLAGS=["/O2", "/fp:fast"])
    else:
        env.Append(CXXFLAGS=["-O3", "-ffast-math"])
        # SSE2 on desktop x86, NEON is on by default for arm64.
        if env["arch"] in ("x86_64", "x86_32"):
            env.Append(CXXFLAGS=["-msse2"])

if env["platform"] == "macos":
    library = env.SharedLibrary(
        "demo/addons/motion_matching/bin/libmotionmatching.{}.{}.framework/libmotionmatching.{}.{}".format(
            env["platform"], env["target"], env["platform"], env["target"]
        ),
        source=sources,
    )
elif env["platform"] == "ios":
    library = env.SharedLibrary(
        "demo/addons/motion_matching/bin/libmotionmatching.{}.{}.a".format(env["platform"], env["target"]),
        source=sources,
    )
else:
    library = env.SharedLibrary(
        "demo/addons/motion_matching/bin/libmotionmatching{}{}".format(env["suffix"], env["SHLIBSUFFIX"]),
        source=sources,
    )

Default(library)
