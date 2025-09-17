"""Build rules and transitions for firmware."""


def _binary_impl(ctx):
    out = ctx.actions.declare_file(ctx.label.name)
    ctx.actions.symlink(output = out, target_file = ctx.executable.binary)
    return [DefaultInfo(files = depset([out]), executable = out)]

def _stm32l4xx_transition_impl(settings, attr):
    # buildifier: disable=unused-variable
    _ignore = attr
    return {
        "//command_line_option:platforms": "//targets/stm32l4xx:platform",
    }

_stm32l4xx_transition = transition(
    implementation = _stm32l4xx_transition_impl,
    inputs = [],
    outputs = [
        "//command_line_option:platforms",
    ],
)

# TODO(tpudlik): Replace this with platform_data when it is available.
stm32l4xx_cc_binary = rule(
    _binary_impl,
    attrs = {
        "binary": attr.label(
            doc = "cc_binary to build for stm32l4xx",
            cfg = _stm32l4xx_transition,
            executable = True,
            mandatory = True,
        ),
        "_allowlist_function_transition": attr.label(default = "@bazel_tools//tools/allowlists/function_transition_allowlist"),
    },
)
