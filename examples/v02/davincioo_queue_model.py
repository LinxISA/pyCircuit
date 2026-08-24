import agentic_circuit as ac


@ac.struct
class WorkItem:
    sequence_id: ac.u8
    opcode: ac.u8
    route: ac.u2
    remaining: ac.u16
    value: ac.u64


@ac.system
def davincioo_queue_model() -> None:
    trace = ac.source(WorkItem, depth=16, latency=1)

    with ac.scope("frontend"):
        prepared = trace.apply(
            lambda item: item.with_fields(value=item.value + 1),
            depth=4,
            latency=1,
        )

    with ac.scope("dispatch"):
        scalar, vector, cube, tma = prepared.route(
        outputs=4,
        key=lambda item: item.route,
        depth=8,
            latency=1,
        )

    with ac.scope("scalar_engine"):
        while scalar.remaining > 0:
            scalar = scalar.apply(
                lambda item: item.with_fields(remaining=item.remaining - 1)
            )
        scalar_done = scalar.apply(
            lambda item: item.with_fields(value=item.value + 1)
        )
    with ac.scope("vector_engine"):
        while vector.remaining > 0:
            vector = vector.apply(
                lambda item: item.with_fields(remaining=item.remaining - 1)
            )
        vector_done = vector.apply(
            lambda item: item.with_fields(value=item.value + 2)
        )
        ac.observe(vector_done)
    with ac.scope("cube_engine"):
        while cube.remaining > 0:
            cube = cube.apply(
                lambda item: item.with_fields(remaining=item.remaining - 1)
            )
        cube_done = cube.apply(
            lambda item: item.with_fields(value=item.value + 3)
        )
    with ac.scope("tma_engine"):
        while tma.remaining > 0:
            tma = tma.apply(
                lambda item: item.with_fields(remaining=item.remaining - 1)
            )
        tma_done = tma.apply(
            lambda item: item.with_fields(value=item.value + 4)
        )
        ac.observe(tma_done)

    completed = scalar_done.merge(
        vector_done,
        cube_done,
        tma_done,
        policy="round_robin",
        depth=8,
        latency=1,
    )
    ac.observe(completed)

    ordered = completed.reorder(
        key=lambda item: item.sequence_id,
        capacity=64,
        start=0,
        depth=8,
        latency=1,
    )

    with ac.scope("retire"):
        retired = ordered.apply(
            lambda item: item.with_fields(
                value=item.value + 100,
            )
        )

    ac.sink(retired)
