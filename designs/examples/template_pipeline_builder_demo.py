from __future__ import annotations

from pycircuit import Circuit, ConnectorStruct, compile, meta, module, const, u


@const
def _pipe_struct(m: Circuit, *, width: int):
    _ = m
    base = (
        meta.struct("pipe")
        .field("payload.data", width=width)
        .field("ctrl.valid", width=1)
        .field("ctrl.ready", width=1)
        .build()
    )
    return base.remove_field("ctrl.ready").rename_field("payload.data", "word").select_fields(
        ["payload.word", "ctrl.valid"]
    )


@module
def build(m: Circuit, *, width: int = 32):
    clk = m.clock("clk")
    rst = m.reset("rst")
    clk_c = m.as_connector(clk, name="clk")
    rst_c = m.as_connector(rst, name="rst")

    s = _pipe_struct(m, width=width)
    in_b = m.inputs(s, prefix="in_")

    st0 = m.state(s, clk=clk_c, rst=rst_c, prefix="st0_")
    m.connect(st0, in_b)

    st1_in = st0.flatten()
    st1_in["payload.word"] = m.as_connector((st0["payload.word"].read() + u(width, 1))[0:width], name="payload_word")

    st1 = m.state(s, clk=clk_c, rst=rst_c, prefix="st1_")
    m.connect(st1, ConnectorStruct(st1_in, spec=s))

    m.outputs(s, st1, prefix="out_")


build.__pycircuit_name__ = "template_pipeline_builder_demo"


if __name__ == "__main__":
    print(compile(build, name="template_pipeline_builder_demo", width=32).emit_mlir())
