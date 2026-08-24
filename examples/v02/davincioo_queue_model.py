import agentic_circuit as ac


@ac.struct
class WorkItem:
    value: int
    route: int
    remaining: int


@ac.system
def davincioo_queue_model() -> None:
    trace = ac.source(WorkItem, depth=8, latency=1)

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
            depth=2,
            latency=1,
        )

    with ac.scope("scalar_engine"):
        scalar_done = scalar.apply(
            lambda item: item.with_fields(value=item.value + 1)
        )
    with ac.scope("vector_engine"):
        vector_done = vector.apply(
            lambda item: item.with_fields(value=item.value + 2)
        )
    with ac.scope("cube_engine"):
        cube_done = cube.apply(
            lambda item: item.with_fields(value=item.value + 3)
        )
    with ac.scope("tma_engine"):
        tma_done = tma.apply(
            lambda item: item.with_fields(value=item.value + 4)
        )

    completed = scalar_done.merge(
        vector_done,
        cube_done,
        tma_done,
        policy="round_robin",
        depth=8,
        latency=1,
    )

    with ac.scope("retire"):
        retired = completed.apply(
            lambda item: item.with_fields(
                value=item.value + 100,
                remaining=item.remaining - 1,
            )
        )

    ac.sink(retired)
