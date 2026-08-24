import agentic_circuit as ac


@ac.struct
class MemoryRequest:
    address: ac.u4
    write: ac.u1
    data: ac.u16
    tag: ac.u8


@ac.system
def pyc_memory_pipeline() -> None:
    requests = ac.source(MemoryRequest, depth=4, latency=1)
    responses = requests.memory(
        address=lambda item: item.address,
        write=lambda item: item.write,
        data=lambda item: item.data,
        entries=16,
        init=0,
        result_field="data",
        depth=4,
        latency=1,
    )
    ac.sink(responses)
