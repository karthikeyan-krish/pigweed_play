"""Build rules and transitions for firmware."""

def _binary_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.symlink(
        output = out,
        target_file = ctx.executable.binary,
    )
    return [DefaultInfo(files = depset([out]), executable = out)]

def _stm32l4xx_transition_impl(settings, attr):
    _ = settings

    # platforms option expects a LIST of LABEL STRINGS
    platform = attr.platform if attr.platform else "//targets/stm32l4xx:application_platform"

    return {
        "//command_line_option:platforms": [platform],
    }

_stm32l4xx_transition = transition(
    implementation = _stm32l4xx_transition_impl,
    inputs = [],
    outputs = ["//command_line_option:platforms"],
)

stm32l4xx_cc_binary = rule(
    implementation = _binary_impl,
    attrs = {
        "binary": attr.label(
            doc = "cc_binary to build for stm32l4xx",
            cfg = _stm32l4xx_transition,
            executable = True,
            mandatory = True,
        ),
        # IMPORTANT: make this a string label, not attr.label, so we can pass it through easily
        "platform": attr.string(
            doc = "Platform label string (e.g. //targets/stm32l4xx:application_platform).",
            mandatory = False,
        ),
        "_allowlist_function_transition": attr.label(
            default = "@bazel_tools//tools/allowlists/function_transition_allowlist",
        ),
    },
)