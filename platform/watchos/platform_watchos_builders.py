"""Functions used to generate source files during build time"""

from platform_methods import generate_bundle_apple_embedded


def generate_bundle(target, source, env):
    generate_bundle_apple_embedded("watchos", "watchos-arm64_arm64_32", "watchos-arm64-simulator", False, target, source, env)
