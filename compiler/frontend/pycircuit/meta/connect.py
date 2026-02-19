from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Mapping

from ..connectors import Connector, ConnectorBundle, ConnectorError, ConnectorStruct
from ..dsl import Signal
from ..hw import Circuit
from ..literals import LiteralValue
from .types import BundleSpec, InterfaceSpec, StagePipeSpec, StructSpec


@dataclass(frozen=True)
class _I1Field:
    width: int = 1
    signed: bool = False


@dataclass(frozen=True)
class SpecBinding:
    spec: BundleSpec | InterfaceSpec | StagePipeSpec | StructSpec
    value: ConnectorBundle | ConnectorStruct | Mapping[str, Any]


def bind(
    spec: BundleSpec | InterfaceSpec | StagePipeSpec | StructSpec,
    value: ConnectorBundle | ConnectorStruct | Mapping[str, Any],
) -> SpecBinding:
    return SpecBinding(spec=spec, value=value)


def _normalize_prefix(prefix: str | None) -> str:
    p = "" if prefix is None else str(prefix)
    return p


def _path_port_name(path: str) -> str:
    return str(path).replace(".", "_")


def _iter_fields(spec: BundleSpec | InterfaceSpec | StagePipeSpec | StructSpec) -> list[tuple[str, Any, str]]:
    out: list[tuple[str, Any, str]] = []
    if isinstance(spec, BundleSpec):
        for f in spec.fields:
            out.append((f.name, f, f.name))
        return out

    if isinstance(spec, InterfaceSpec):
        for b in spec.bundles:
            for f in b.fields:
                key = f"{b.name}_{f.name}"
                out.append((key, f, key))
        return out

    if isinstance(spec, StagePipeSpec):
        for f in spec.payload.fields:
            out.append((f.name, f, f.name))
        if spec.has_valid:
            out.append((spec.valid_name, _I1Field(), spec.valid_name))
        if spec.has_ready:
            out.append((spec.ready_name, _I1Field(), spec.ready_name))
        return out

    if isinstance(spec, StructSpec):
        seen_ports: set[str] = set()
        for path, fld in spec.flatten_fields():
            pname = _path_port_name(path)
            if pname in seen_ports:
                raise ConnectorError(
                    f"struct {spec.name!r} has colliding port name {pname!r}; use unique field paths"
                )
            seen_ports.add(pname)
            out.append((path, fld, pname))
        return out

    raise TypeError(f"expected BundleSpec/InterfaceSpec/StagePipeSpec/StructSpec, got {type(spec).__name__}")


def _port_name(prefix: str | None, local_name: str) -> str:
    p = _normalize_prefix(prefix)
    return f"{p}{local_name}"


def _connector_width(c: Connector) -> int:
    ty = str(c.ty)
    if not ty.startswith("i"):
        raise ConnectorError(f"expected integer connector type, got {ty!r}")
    try:
        w = int(ty[1:])
    except ValueError as e:
        raise ConnectorError(f"invalid integer connector type: {ty!r}") from e
    if w <= 0:
        raise ConnectorError(f"invalid integer connector width: {ty!r}")
    return w


def _connector_signed(c: Connector) -> bool:
    if hasattr(c, "signed"):
        return bool(getattr(c, "signed"))
    rv = c.read()
    return bool(getattr(rv, "signed", False))


def _values_mapping(values: ConnectorBundle | ConnectorStruct | Mapping[str, Any]) -> Mapping[str, Any]:
    if isinstance(values, ConnectorBundle):
        return {k: v for k, v in values.items()}
    if isinstance(values, ConnectorStruct):
        return values.flatten()
    return dict(values)


def _inputs_bundle(
    m: Circuit,
    spec: BundleSpec | InterfaceSpec | StagePipeSpec | StructSpec,
    *,
    prefix: str | None = None,
) -> ConnectorBundle:
    out: dict[str, Connector] = {}
    for key, f, pname in _iter_fields(spec):
        out[key] = m.input_connector(_port_name(prefix, pname), width=int(f.width), signed=bool(getattr(f, "signed", False)))
    return ConnectorBundle(out)


def _outputs_bundle(
    m: Circuit,
    spec: BundleSpec | InterfaceSpec | StagePipeSpec | StructSpec,
    values: ConnectorBundle | ConnectorStruct | Mapping[str, Any],
    *,
    prefix: str | None = None,
) -> ConnectorBundle:
    vals = dict(_values_mapping(values))

    exp = [k for k, _, _ in _iter_fields(spec)]
    got = sorted(str(k) for k in vals.keys())
    missing = sorted(set(exp) - set(got))
    extra = sorted(set(got) - set(exp))
    if missing or extra:
        parts: list[str] = []
        if missing:
            parts.append("missing: " + ", ".join(missing))
        if extra:
            parts.append("extra: " + ", ".join(extra))
        raise ConnectorError(f"outputs key mismatch ({'; '.join(parts)})")

    out: dict[str, Connector] = {}
    for key, f, pname in _iter_fields(spec):
        c = m.as_connector(vals[key], name=key)
        got_w = _connector_width(c)
        exp_w = int(f.width)
        if got_w != exp_w:
            raise ConnectorError(f"outputs[{key!r}] width mismatch: expected i{exp_w}, got i{got_w}")
        exp_signed = bool(getattr(f, "signed", False))
        got_signed = _connector_signed(c)
        if got_signed != exp_signed:
            raise ConnectorError(
                f"outputs[{key!r}] signedness mismatch: expected signed={exp_signed}, got signed={got_signed}"
            )
        out[key] = m.output_connector(_port_name(prefix, pname), c)
    return ConnectorBundle(out)


def _as_signal(v: Connector | Signal, *, ctx: str) -> Signal:
    if isinstance(v, Connector):
        rv = v.read()
        if not isinstance(rv, Signal):
            raise ConnectorError(f"{ctx}: expected clock/reset connector payload Signal, got {type(rv).__name__}")
        return rv
    if isinstance(v, Signal):
        return v
    raise ConnectorError(f"{ctx}: expected Connector or Signal, got {type(v).__name__}")


def _resolve_init(init: Mapping[str, Any] | Any, key: str) -> Any:
    if isinstance(init, Mapping):
        if key in init:
            return init[key]
        if "*" in init:
            return init["*"]
        return 0
    return init


def _state_bundle(
    m: Circuit,
    spec: BundleSpec | InterfaceSpec | StagePipeSpec | StructSpec,
    *,
    clk: Connector | Signal,
    rst: Connector | Signal,
    prefix: str | None = None,
    init: Mapping[str, Any] | Any = 0,
    en: Connector | Signal | int | LiteralValue = 1,
) -> ConnectorBundle:
    clk_sig = _as_signal(clk, ctx="_state_bundle(clk)")
    rst_sig = _as_signal(rst, ctx="_state_bundle(rst)")
    out: dict[str, Connector] = {}
    for key, f, pname in _iter_fields(spec):
        out[key] = m.reg_connector(
            _port_name(prefix, pname),
            clk=clk_sig,
            rst=rst_sig,
            width=int(f.width),
            init=_resolve_init(init, key),
            en=en,
        )
    return ConnectorBundle(out)


def _inputs_struct(m: Circuit, spec: StructSpec, *, prefix: str | None = None) -> ConnectorStruct:
    if not isinstance(spec, StructSpec):
        raise TypeError(f"inputs expects StructSpec, got {type(spec).__name__}")
    bundle = _inputs_bundle(m, spec, prefix=prefix)
    return ConnectorStruct.from_flat({k: v for k, v in bundle.items()}, spec=spec)


def _outputs_struct(
    m: Circuit,
    spec: StructSpec,
    values: ConnectorStruct | Mapping[str, Any],
    *,
    prefix: str | None = None,
) -> ConnectorStruct:
    if not isinstance(spec, StructSpec):
        raise TypeError(f"outputs expects StructSpec, got {type(spec).__name__}")
    bundle = _outputs_bundle(m, spec, values, prefix=prefix)
    return ConnectorStruct.from_flat({k: v for k, v in bundle.items()}, spec=spec)


def _state_struct(
    m: Circuit,
    spec: StructSpec,
    *,
    clk: Connector | Signal,
    rst: Connector | Signal,
    prefix: str | None = None,
    init: Mapping[str, Any] | Any = 0,
    en: Connector | Signal | int | LiteralValue = 1,
) -> ConnectorStruct:
    if not isinstance(spec, StructSpec):
        raise TypeError(f"state expects StructSpec, got {type(spec).__name__}")
    bundle = _state_bundle(m, spec, clk=clk, rst=rst, prefix=prefix, init=init, en=en)
    return ConnectorStruct.from_flat({k: v for k, v in bundle.items()}, spec=spec)


def inputs(
    m: Circuit,
    spec: BundleSpec | InterfaceSpec | StagePipeSpec | StructSpec,
    *,
    prefix: str | None = None,
) -> ConnectorBundle | ConnectorStruct:
    if isinstance(spec, StructSpec):
        return _inputs_struct(m, spec, prefix=prefix)
    return _inputs_bundle(m, spec, prefix=prefix)


def outputs(
    m: Circuit,
    spec: BundleSpec | InterfaceSpec | StagePipeSpec | StructSpec,
    values: ConnectorBundle | ConnectorStruct | Mapping[str, Any],
    *,
    prefix: str | None = None,
) -> ConnectorBundle | ConnectorStruct:
    if isinstance(spec, StructSpec):
        return _outputs_struct(m, spec, values, prefix=prefix)
    return _outputs_bundle(m, spec, values, prefix=prefix)


def state(
    m: Circuit,
    spec: BundleSpec | InterfaceSpec | StagePipeSpec | StructSpec,
    *,
    clk: Connector | Signal,
    rst: Connector | Signal,
    prefix: str | None = None,
    init: Mapping[str, Any] | Any = 0,
    en: Connector | Signal | int | LiteralValue = 1,
) -> ConnectorBundle | ConnectorStruct:
    if isinstance(spec, StructSpec):
        return _state_struct(m, spec, clk=clk, rst=rst, prefix=prefix, init=init, en=en)
    return _state_bundle(m, spec, clk=clk, rst=rst, prefix=prefix, init=init, en=en)


def ports(
    m: Circuit,
    spec_bindings: Mapping[
        str,
        Connector | ConnectorBundle | ConnectorStruct | Mapping[str, Any] | SpecBinding | tuple[Any, Any],
    ],
) -> dict[str, Connector]:
    out: dict[str, Connector] = {}
    for pname, bound in spec_bindings.items():
        p = str(pname)
        if isinstance(bound, tuple) and len(bound) == 2:
            sp0, sp1 = bound
            if isinstance(sp0, (BundleSpec, InterfaceSpec, StagePipeSpec, StructSpec)):
                bound = SpecBinding(spec=sp0, value=sp1)

        if isinstance(bound, SpecBinding):
            spec = bound.spec
            vals = dict(_values_mapping(bound.value))

            exp_keys = [k for k, _, _ in _iter_fields(spec)]
            got_keys = sorted(str(k) for k in vals.keys())
            missing = sorted(set(exp_keys) - set(got_keys))
            extra = sorted(set(got_keys) - set(exp_keys))
            if missing or extra:
                parts: list[str] = []
                if missing:
                    parts.append("missing: " + ", ".join(missing))
                if extra:
                    parts.append("extra: " + ", ".join(extra))
                raise ConnectorError(f"ports[{p!r}] key mismatch ({'; '.join(parts)})")

            for key, field, pname_local in _iter_fields(spec):
                port = f"{p}_{pname_local}"
                c = m.as_connector(vals[key], name=port)
                exp_w = int(getattr(field, "width", 0))
                got_w = _connector_width(c)
                if got_w != exp_w:
                    raise ConnectorError(
                        f"ports[{p!r}][{key!r}] width mismatch: expected i{exp_w}, got i{got_w}"
                    )
                exp_signed = bool(getattr(field, "signed", False))
                got_signed = _connector_signed(c)
                if exp_signed != got_signed:
                    raise ConnectorError(
                        f"ports[{p!r}][{key!r}] signedness mismatch: "
                        + f"expected signed={exp_signed}, got signed={got_signed}"
                    )
                out[port] = c
            continue

        if isinstance(bound, Connector):
            out[p] = m.as_connector(bound, name=p)
            continue
        if isinstance(bound, ConnectorBundle):
            for k, v in bound.items():
                port = f"{p}_{k}"
                out[port] = m.as_connector(v, name=port)
            continue
        if isinstance(bound, ConnectorStruct):
            for k, v in bound.items():
                port = f"{p}_{_path_port_name(k)}"
                out[port] = m.as_connector(v, name=port)
            continue
        if isinstance(bound, Mapping):
            for k, v in bound.items():
                port = f"{p}_{str(k)}"
                out[port] = m.as_connector(v, name=port)
            continue
        raise ConnectorError(
            "ports: value for "
            + f"{p!r} must be Connector/ConnectorBundle/ConnectorStruct/mapping, got {type(bound).__name__}"
        )
    return out


def connect(
    m: Circuit,
    dst: Connector | ConnectorBundle | ConnectorStruct,
    src: Connector | ConnectorBundle | ConnectorStruct,
    *,
    when: Signal | Connector | int | LiteralValue = 1,
) -> None:
    if isinstance(dst, ConnectorStruct) and isinstance(src, ConnectorStruct):
        dkeys = sorted(dst.keys())
        skeys = sorted(src.keys())
        if dkeys != skeys:
            raise ConnectorError(f"connect key mismatch: dst={dkeys} src={skeys}")
        for k in dkeys:
            dw = _connector_width(dst.flatten()[k])
            sw = _connector_width(src.flatten()[k])
            if dw != sw:
                raise ConnectorError(f"connect[{k!r}] width mismatch: dst=i{dw} src=i{sw}")
        for k in dkeys:
            m.connect(dst.flatten()[k], src.flatten()[k], when=when)
        return

    if isinstance(dst, ConnectorBundle) and isinstance(src, ConnectorBundle):
        dkeys = sorted(dst.keys())
        skeys = sorted(src.keys())
        if dkeys != skeys:
            raise ConnectorError(f"connect key mismatch: dst={dkeys} src={skeys}")
        for k in dkeys:
            dw = _connector_width(dst[k])
            sw = _connector_width(src[k])
            if dw != sw:
                raise ConnectorError(f"connect[{k!r}] width mismatch: dst=i{dw} src=i{sw}")
    m.connect(dst, src, when=when)


def _connect_struct_only(
    m: Circuit,
    dst: ConnectorStruct,
    src: ConnectorStruct,
    *,
    when: Signal | Connector | int | LiteralValue = 1,
) -> None:
    connect(m, dst, src, when=when)
