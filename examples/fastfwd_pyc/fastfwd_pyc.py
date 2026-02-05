from __future__ import annotations

from dataclasses import dataclass

from pycircuit import Circuit, Queue, Reg, Wire, cat


def _is_pow2(x: int) -> bool:
    return x > 0 and (x & (x - 1)) == 0


@dataclass(frozen=True)
class _Wheel:
    valid: list[Reg]
    seq: list[Reg]


@dataclass(frozen=True)
class _Rob:
    valid: list[Reg]
    data: list[Reg]


@dataclass(frozen=True)
class _Hist:
    valid: list[Reg]
    seq: list[Reg]
    data: list[Reg]


def _wheel_read(valid: list[Reg], seq: list[Reg], *, idx: Wire, zero_seq: Wire) -> tuple[Wire, Wire]:
    """Read (valid, seq) at dynamic `idx` from a small power-of-two wheel."""
    if not valid:
        raise ValueError("wheel cannot be empty")
    if len(valid) != len(seq):
        raise ValueError("wheel valid/seq size mismatch")

    m = valid[0].q.m  # type: ignore[assignment]
    out_v = m.const_wire(0, width=1)
    out_s = zero_seq
    for s in range(len(valid)):
        is_s = idx.eq(s)
        out_v = is_s.select(valid[s].out(), out_v)
        out_s = is_s.select(seq[s].out(), out_s)
    return out_v, out_s


def _wheel_slot_busy(valid: list[Reg], *, idx: Wire) -> Wire:
    m = valid[0].q.m  # type: ignore[assignment]
    busy = m.const_wire(0, width=1)
    for s in range(len(valid)):
        busy = busy | (idx.eq(s) & valid[s].out())
    return busy


def _wheel_update(
    valid: list[Reg],
    seq: list[Reg],
    *,
    set_en: Wire,
    set_idx: Wire,
    set_seq: Wire,
    clear_en: Wire,
    clear_idx: Wire,
) -> None:
    """Update a wheel with one optional set and one optional clear (different slots)."""
    m = valid[0].q.m  # type: ignore[assignment]
    for s in range(len(valid)):
        v_cur = valid[s].out()
        s_cur = seq[s].out()

        do_clear = clear_en & clear_idx.eq(s)
        do_set = set_en & set_idx.eq(s)

        v_next = do_set.select(m.const_wire(1, width=1), do_clear.select(m.const_wire(0, width=1), v_cur))
        s_next = do_set.select(set_seq, s_cur)

        valid[s].set(v_next)
        seq[s].set(s_next)


def _rob_update(
    rob: _Rob,
    *,
    exp_seq: Wire,
    commit_pop: Wire,
    ins_fire: Wire,
    ins_seq: Wire,
    ins_data: Wire,
    depth: int,
) -> None:
    """Update per-lane ROB (insert by seq delta, then optional pop/shift by 1)."""
    m = exp_seq.m  # type: ignore[assignment]
    if depth <= 0:
        raise ValueError("ROB depth must be > 0")
    if len(rob.valid) != depth or len(rob.data) != depth:
        raise ValueError("ROB depth mismatch")

    delta = (ins_seq - exp_seq) >> 2

    v_ins: list[Wire] = []
    d_ins: list[Wire] = []
    for i in range(depth):
        hit = ins_fire & delta.eq(i)
        v_ins.append(hit.select(m.const_wire(1, width=1), rob.valid[i].out()))
        d_ins.append(hit.select(ins_data, rob.data[i].out()))

    for i in range(depth):
        if i + 1 < depth:
            v_next = commit_pop.select(v_ins[i + 1], v_ins[i])
            d_next = commit_pop.select(d_ins[i + 1], d_ins[i])
        else:
            v_next = commit_pop.select(m.const_wire(0, width=1), v_ins[i])
            d_next = commit_pop.select(m.const_wire(0, width=ins_data.width), d_ins[i])
        rob.valid[i].set(v_next)
        rob.data[i].set(d_next)


def _hist_shift_insert(hist: _Hist, *, k: Wire, seqs: list[Wire], datas: list[Wire]) -> None:
    """Shift history down by k (0..4) and insert up to 4 newest entries (seqs/datas)."""
    m = hist.valid[0].q.m  # type: ignore[assignment]
    depth = len(hist.valid)
    if depth != len(hist.seq) or depth != len(hist.data):
        raise ValueError("hist size mismatch")
    if depth < 8:
        raise ValueError("hist depth must be >= 8 (prototype)")
    if len(seqs) != 4 or len(datas) != 4:
        raise ValueError("expected exactly 4 commit slots")

    # k is i3 (0..4). Build per-index muxes for next state.
    for i in range(depth):
        v0 = hist.valid[i].out()
        s0 = hist.seq[i].out()
        d0 = hist.data[i].out()

        # Defaults: no shift (k=0).
        v_next = v0
        s_next = s0
        d_next = d0

        # k==1: [new3, old0, old1, ...]
        if i == 0:
            v_k1 = m.const_wire(1, width=1)
            s_k1 = seqs[0]
            d_k1 = datas[0]
        else:
            v_k1 = hist.valid[i - 1].out()
            s_k1 = hist.seq[i - 1].out()
            d_k1 = hist.data[i - 1].out()

        # k==2: [new3, new2, old0, old1, ...]
        if i == 0:
            v_k2 = m.const_wire(1, width=1)
            s_k2 = seqs[1]
            d_k2 = datas[1]
        elif i == 1:
            v_k2 = m.const_wire(1, width=1)
            s_k2 = seqs[0]
            d_k2 = datas[0]
        else:
            v_k2 = hist.valid[i - 2].out()
            s_k2 = hist.seq[i - 2].out()
            d_k2 = hist.data[i - 2].out()

        # k==3: [new3, new2, new1, old0, ...]
        if i == 0:
            v_k3 = m.const_wire(1, width=1)
            s_k3 = seqs[2]
            d_k3 = datas[2]
        elif i == 1:
            v_k3 = m.const_wire(1, width=1)
            s_k3 = seqs[1]
            d_k3 = datas[1]
        elif i == 2:
            v_k3 = m.const_wire(1, width=1)
            s_k3 = seqs[0]
            d_k3 = datas[0]
        else:
            v_k3 = hist.valid[i - 3].out()
            s_k3 = hist.seq[i - 3].out()
            d_k3 = hist.data[i - 3].out()

        # k==4: [new3, new2, new1, new0, old0, ...]
        if i == 0:
            v_k4 = m.const_wire(1, width=1)
            s_k4 = seqs[3]
            d_k4 = datas[3]
        elif i == 1:
            v_k4 = m.const_wire(1, width=1)
            s_k4 = seqs[2]
            d_k4 = datas[2]
        elif i == 2:
            v_k4 = m.const_wire(1, width=1)
            s_k4 = seqs[1]
            d_k4 = datas[1]
        elif i == 3:
            v_k4 = m.const_wire(1, width=1)
            s_k4 = seqs[0]
            d_k4 = datas[0]
        else:
            v_k4 = hist.valid[i - 4].out()
            s_k4 = hist.seq[i - 4].out()
            d_k4 = hist.data[i - 4].out()

        is1 = k.eq(1)
        is2 = k.eq(2)
        is3 = k.eq(3)
        is4 = k.eq(4)

        v_next = is4.select(v_k4, is3.select(v_k3, is2.select(v_k2, is1.select(v_k1, v_next))))
        s_next = is4.select(s_k4, is3.select(s_k3, is2.select(s_k2, is1.select(s_k1, s_next))))
        d_next = is4.select(d_k4, is3.select(d_k3, is2.select(d_k2, is1.select(d_k1, d_next))))

        hist.valid[i].set(v_next)
        hist.seq[i].set(s_next)
        hist.data[i].set(d_next)


def _build_fastfwd(
    m: Circuit,
    ENG_PER_LANE: int = 2,
    LANE_Q_DEPTH: int = 32,
    ENG_Q_DEPTH: int = 8,
    ROB_DEPTH: int = 16,
    SEQ_W: int = 16,
    WHEEL: int = 8,
    HIST_DEPTH: int = 8,
) -> None:
    # ---- parameters (JIT-time) ----
    eng_per_lane = int(ENG_PER_LANE)
    if eng_per_lane <= 0:
        raise ValueError("ENG_PER_LANE must be > 0")
    total_eng = 4 * eng_per_lane
    if total_eng > 32:
        raise ValueError("total engines must be <= 32")

    lane_q_depth = int(LANE_Q_DEPTH)
    eng_q_depth = int(ENG_Q_DEPTH)
    rob_depth = int(ROB_DEPTH)
    seq_w = int(SEQ_W)
    wheel = int(WHEEL)
    hist_depth = int(HIST_DEPTH)

    if lane_q_depth <= 0 or eng_q_depth <= 0 or rob_depth <= 0:
        raise ValueError("queue/rob depths must be > 0")
    if seq_w <= 1:
        raise ValueError("SEQ_W must be > 1")
    if wheel <= 0 or not _is_pow2(wheel):
        raise ValueError("WHEEL must be a power-of-two > 0")
    if wheel < 8:
        raise ValueError("WHEEL must be >= 8 (latency 1..4 + JIT scheduling margin)")
    if hist_depth < 8:
        raise ValueError("HIST_DEPTH must be >= 8 (dependency window <= 7)")

    wheel_bits = (wheel - 1).bit_length()
    bundle_w = seq_w + 128 + 5
    comp_w = seq_w + 128

    # ---- ports ----
    clk = m.clock("clk")
    rst = m.reset("rst")

    pkt_in_vld = [m.in_wire(f"lane{i}_pkt_in_vld", width=1) for i in range(4)]
    pkt_in_data = [m.in_wire(f"lane{i}_pkt_in_data", width=128) for i in range(4)]
    pkt_in_ctrl = [m.in_wire(f"lane{i}_pkt_in_ctrl", width=5) for i in range(4)]

    # Registered outputs (per spec).
    bkpr_r = m.out("pkt_in_bkpr", clk=clk, rst=rst, width=1, init=0)
    out_vld_r = [m.out(f"lane{i}_pkt_out_vld", clk=clk, rst=rst, width=1, init=0) for i in range(4)]
    out_data_r = [m.out(f"lane{i}_pkt_out_data", clk=clk, rst=rst, width=128, init=0) for i in range(4)]

    # Forwarding Engine interface (per-engine scalar ports).
    fwded_vld = [m.in_wire(f"fwded{e}_pkt_data_vld", width=1) for e in range(total_eng)]
    fwded_data = [m.in_wire(f"fwded{e}_pkt_data", width=128) for e in range(total_eng)]

    # ---- global regs ----
    with m.scope("TIME"):
        cycle = m.out("cycle", clk=clk, rst=rst, width=16, init=0)
        cycle.set(cycle.out() + 1)
        cycle_mod = cycle.out()[0:wheel_bits]

        seq_alloc = m.out("seq_alloc", clk=clk, rst=rst, width=seq_w, init=0)

    commit_lane = m.out("commit_lane", clk=clk, rst=rst, width=2, init=0)

    # Expected seq per output lane (seq%4==lane).
    exp_seq = [m.out(f"lane{i}__exp_seq", clk=clk, rst=rst, width=seq_w, init=i) for i in range(4)]

    # ---- per-lane issue queues ----
    issue_q: list[Queue] = []
    for lane in range(4):
        issue_q.append(m.queue(f"lane{lane}__issue_q", clk=clk, rst=rst, width=bundle_w, depth=lane_q_depth))

    # ---- per-engine wheels + completion queues ----
    wheels: list[_Wheel] = []
    comp_q: list[Queue] = []
    for e in range(total_eng):
        wv = [m.out(f"eng{e}__wheel_v{s}", clk=clk, rst=rst, width=1, init=0) for s in range(wheel)]
        ws = [m.out(f"eng{e}__wheel_seq{s}", clk=clk, rst=rst, width=seq_w, init=0) for s in range(wheel)]
        wheels.append(_Wheel(valid=wv, seq=ws))
        comp_q.append(m.queue(f"eng{e}__comp_q", clk=clk, rst=rst, width=comp_w, depth=eng_q_depth))

    # ---- per-lane ROBs ----
    robs: list[_Rob] = []
    for lane in range(4):
        rv = [m.out(f"lane{lane}__rob_v{i}", clk=clk, rst=rst, width=1, init=0) for i in range(rob_depth)]
        rd = [m.out(f"lane{lane}__rob_d{i}", clk=clk, rst=rst, width=128, init=0) for i in range(rob_depth)]
        robs.append(_Rob(valid=rv, data=rd))

    # ---- dependency history (global shift register) ----
    hist = _Hist(
        valid=[m.out(f"hist_v{i}", clk=clk, rst=rst, width=1, init=0) for i in range(hist_depth)],
        seq=[m.out(f"hist_seq{i}", clk=clk, rst=rst, width=seq_w, init=0) for i in range(hist_depth)],
        data=[m.out(f"hist_d{i}", clk=clk, rst=rst, width=128, init=0) for i in range(hist_depth)],
    )

    # ---- input accept (PKTIN -> issue queues) ----
    with m.scope("IN"):
        bkpr = bkpr_r.out()
        accept = ~bkpr

        eff_v = [pkt_in_vld[i] & accept for i in range(4)]
        inc = [eff_v[i].select(m.const_wire(1, width=seq_w), m.const_wire(0, width=seq_w)) for i in range(4)]

        base = seq_alloc.out()
        seq_lane = [base, base + inc[0], base + inc[0] + inc[1], base + inc[0] + inc[1] + inc[2]]
        total_inc = inc[0] + inc[1] + inc[2] + inc[3]
        seq_alloc.set(base + total_inc)

        # Map each accepted packet into its output-lane issue queue by seq%4.
        push_v = [m.const_wire(0, width=1) for _ in range(4)]
        push_d = [m.const_wire(0, width=bundle_w) for _ in range(4)]

        for i in range(4):
            seq_i = seq_lane[i]
            lane_i = seq_i[0:2]  # seq%4
            bundle_i = cat(seq_i, pkt_in_data[i], pkt_in_ctrl[i])
            for lane in range(4):
                hit = eff_v[i] & lane_i.eq(lane)
                push_v[lane] = push_v[lane] | hit
                push_d[lane] = hit.select(bundle_i, push_d[lane])

        push_fire = []
        for lane in range(4):
            push_fire.append(issue_q[lane].push(push_d[lane], when=push_v[lane]))

        # Conservative BKPR policy (registered):
        # Assert when any issue queue is "nearly full" after this cycle's push/pop.
        #
        # We approximate "nearly full" by tracking a shadow count for each issue
        # queue and asserting backpressure when count >= DEPTH-2.
        # (Leaves slack for the registered BKPR latency.)
        shadow_cnt = [m.out(f"lane{lane}__iq_cnt", clk=clk, rst=rst, width=16, init=0) for lane in range(4)]

    # ---- dispatch (issue queues -> FE inputs) ----
    with m.scope("DISPATCH"):
        # Default FE outputs.
        fwd_vld = [m.const_wire(0, width=1) for _ in range(total_eng)]
        fwd_data = [m.const_wire(0, width=128) for _ in range(total_eng)]
        fwd_lat = [m.const_wire(0, width=2) for _ in range(total_eng)]
        fwd_dp_vld = [m.const_wire(0, width=1) for _ in range(total_eng)]
        fwd_dp_data = [m.const_wire(0, width=128) for _ in range(total_eng)]

        # Per-lane dispatch signals.
        lane_pop = [m.const_wire(0, width=1) for _ in range(4)]
        lane_pop_seq = [m.const_wire(0, width=seq_w) for _ in range(4)]
        lane_pop_data = [m.const_wire(0, width=128) for _ in range(4)]
        lane_pop_lat = [m.const_wire(0, width=2) for _ in range(4)]
        lane_pop_dp = [m.const_wire(0, width=3) for _ in range(4)]

        # Dependency lookup helpers (history + ROBs).
        def dep_lookup(dep_seq: Wire) -> tuple[Wire, Wire]:
            found_h = m.const_wire(0, width=1)
            data_h = m.const_wire(0, width=128)
            for i in range(hist_depth):
                match = hist.valid[i].out() & (hist.seq[i].out().eq(dep_seq))
                found_h = found_h | match
                data_h = match.select(hist.data[i].out(), data_h)

            dep_lane = dep_seq[0:2]
            found_r = m.const_wire(0, width=1)
            data_r = m.const_wire(0, width=128)
            for lane in range(4):
                is_lane = dep_lane.eq(lane)
                delta = (dep_seq - exp_seq[lane].out()) >> 2
                for s in range(rob_depth):
                    match = is_lane & robs[lane].valid[s].out() & delta.eq(s)
                    found_r = found_r | match
                    data_r = match.select(robs[lane].data[s].out(), data_r)

            found = found_r | found_h
            data = found_r.select(data_r, data_h)
            return found, data

        # Compute dispatch for each lane independently (one issue per lane per cycle).
        dispatch_fire_eng = [m.const_wire(0, width=1) for _ in range(total_eng)]
        dispatch_slot_eng = [m.const_wire(0, width=wheel_bits) for _ in range(total_eng)]
        dispatch_seq_eng = [m.const_wire(0, width=seq_w) for _ in range(total_eng)]

        for lane in range(4):
            qv = issue_q[lane].out_valid
            qb = issue_q[lane].out_data

            ctrl = qb[0:5]
            data_i = qb[5:133]
            seq_i = qb[133 : 133 + seq_w]

            lat = ctrl[0:2]
            dp = ctrl[2:5]

            lane_pop_seq[lane] = seq_i
            lane_pop_data[lane] = data_i
            lane_pop_lat[lane] = lat
            lane_pop_dp[lane] = dp

            dp_present = ~dp.eq(0)
            dep_seq = seq_i - dp.zext(width=seq_w)
            dep_found, dep_data = dep_lookup(dep_seq)
            dep_ok = (~dp_present) | dep_found

            # Completion slot (cycle + 1 + latencyCycles).
            lat3 = lat.zext(width=wheel_bits)
            slot = cycle_mod + lat3 + m.const_wire(2, width=wheel_bits)

            # Pick an engine in this lane's pool with a free slot.
            eng_base = lane * eng_per_lane
            chosen = [m.const_wire(0, width=1) for _ in range(eng_per_lane)]
            any_free = m.const_wire(0, width=1)
            for k in range(eng_per_lane):
                e = eng_base + k
                busy = _wheel_slot_busy(wheels[e].valid, idx=slot)
                free = ~busy
                take = free & ~any_free
                any_free = any_free | free
                chosen[k] = take

            dispatch_ok = dep_ok & any_free
            pop = issue_q[lane].pop(when=dispatch_ok)
            lane_pop[lane] = pop.fire

            # Drive FE signals for the chosen engine.
            for k in range(eng_per_lane):
                e = eng_base + k
                fire_e = pop.fire & chosen[k]
                dispatch_fire_eng[e] = fire_e
                dispatch_slot_eng[e] = slot
                dispatch_seq_eng[e] = seq_i

                fwd_vld[e] = fire_e
                fwd_data[e] = data_i
                fwd_lat[e] = lat
                fwd_dp_vld[e] = dp_present
                fwd_dp_data[e] = dep_data

    # ---- completions (FEOUT -> per-engine completion queue) ----
    with m.scope("COMPLETE"):
        zero_seq = m.const_wire(0, width=seq_w)
        for e in range(total_eng):
            wv_cur, ws_cur = _wheel_read(wheels[e].valid, wheels[e].seq, idx=cycle_mod, zero_seq=zero_seq)
            comp_v = wv_cur & fwded_vld[e]
            comp_bus = cat(ws_cur, fwded_data[e])
            fire = comp_q[e].push(comp_bus, when=comp_v)

            # Clear wheel slot only when we successfully capture the completion.
            _wheel_update(
                wheels[e].valid,
                wheels[e].seq,
                set_en=dispatch_fire_eng[e],
                set_idx=dispatch_slot_eng[e],
                set_seq=dispatch_seq_eng[e],
                clear_en=fire,
                clear_idx=cycle_mod,
            )

    # ---- merge completions into per-lane ROBs (<=1 per lane per cycle) ----
    with m.scope("ROB"):
        ins_fire_lane = [m.const_wire(0, width=1) for _ in range(4)]
        ins_seq_lane = [m.const_wire(0, width=seq_w) for _ in range(4)]
        ins_data_lane = [m.const_wire(0, width=128) for _ in range(4)]

        for lane in range(4):
            eng_base = lane * eng_per_lane

            take = [m.const_wire(0, width=1) for _ in range(eng_per_lane)]
            any_take = m.const_wire(0, width=1)
            sel_bus = m.const_wire(0, width=comp_w)

            for k in range(eng_per_lane):
                e = eng_base + k
                vb = comp_q[e].out_valid
                db = comp_q[e].out_data
                seq_c = db[128 : 128 + seq_w]
                delta = (seq_c - exp_seq[lane].out()) >> 2
                in_range = delta.ult(rob_depth)
                cand = vb & in_range & ~any_take
                take[k] = cand
                any_take = any_take | cand
                sel_bus = cand.select(db, sel_bus)

            for k in range(eng_per_lane):
                e = eng_base + k
                comp_q[e].pop(when=take[k])

            ins_fire_lane[lane] = any_take
            ins_seq_lane[lane] = sel_bus[128 : 128 + seq_w]
            ins_data_lane[lane] = sel_bus[0:128]

    # ---- commit (ROB -> PKTOUT) + history ----
    with m.scope("COMMIT"):
        start = commit_lane.out()

        # Read lane0 entries for each output lane.
        lane_ready = [robs[l].valid[0].out() for l in range(4)]
        lane_data0 = [robs[l].data[0].out() for l in range(4)]

        # Compute commit prefixes for each possible start lane (0..3).
        def prefix(start_lane: int) -> tuple[list[Wire], Wire]:
            v = []
            ok = m.const_wire(1, width=1)
            for k in range(4):
                lane = (start_lane + k) & 3
                ok = ok & lane_ready[lane]
                v.append(ok)
            # commit_count in i3 (0..4).
            cnt = v[0].select(
                m.const_wire(1, width=3),
                m.const_wire(0, width=3),
            )
            cnt = v[1].select(m.const_wire(2, width=3), cnt)
            cnt = v[2].select(m.const_wire(3, width=3), cnt)
            cnt = v[3].select(m.const_wire(4, width=3), cnt)
            return v, cnt

        v0, c0 = prefix(0)
        v1, c1 = prefix(1)
        v2, c2 = prefix(2)
        v3, c3 = prefix(3)

        is0 = start.eq(0)
        is1 = start.eq(1)
        is2 = start.eq(2)
        is3 = start.eq(3)

        commit_cnt = is3.select(c3, is2.select(c2, is1.select(c1, c0)))

        # Output valids/data for physical lanes (registered).
        out_v = [m.const_wire(0, width=1) for _ in range(4)]
        out_d = [m.const_wire(0, width=128) for _ in range(4)]

        # commit_v_s[start_lane][k] means commit (k+1)th packet of this cycle (prefix) when starting at start_lane.
        commit_v_s = [v0, v1, v2, v3]
        for phys in range(4):
            # For each possible start, determine whether phys lane outputs.
            vv = m.const_wire(0, width=1)
            dd = m.const_wire(0, width=128)
            for s in range(4):
                for k in range(4):
                    lane = (s + k) & 3
                    if lane != phys:
                        continue
                    hit = commit_v_s[s][k]
                    vv_s = hit
                    dd_s = hit.select(lane_data0[phys], dd)
                    vv = (start.eq(s) & vv_s).select(m.const_wire(1, width=1), vv)
                    dd = (start.eq(s) & vv_s).select(lane_data0[phys], dd)
            out_v[phys] = vv
            out_d[phys] = dd

        for i in range(4):
            out_vld_r[i].set(out_v[i])
            out_data_r[i].set(out_v[i].select(out_d[i], m.const_wire(0, width=128)))

        # Per-lane pop (shift) when that lane committed.
        commit_pop = out_v

        # Advance commit lane pointer by commit_cnt.
        next_lane = (start + commit_cnt[0:2])[0:2]
        commit_lane.set(next_lane)

        # Update expected seq per lane.
        for lane in range(4):
            inc4 = commit_pop[lane].select(m.const_wire(4, width=seq_w), m.const_wire(0, width=seq_w))
            exp_seq[lane].set(exp_seq[lane].out() + inc4)

        # Update ROBs: insert completion then pop if committed.
        for lane in range(4):
            _rob_update(
                robs[lane],
                exp_seq=exp_seq[lane].out(),
                commit_pop=commit_pop[lane],
                ins_fire=ins_fire_lane[lane],
                ins_seq=ins_seq_lane[lane],
                ins_data=ins_data_lane[lane],
                depth=rob_depth,
            )

        # Build commit seq/data for up to 4 outputs in-order (for history shift).
        commit_seq_slots = [m.const_wire(0, width=seq_w) for _ in range(4)]
        commit_data_slots = [m.const_wire(0, width=128) for _ in range(4)]
        for k in range(4):
            lane_k = (start + m.const_wire(k, width=2))[0:2]
            # Select exp_seq/data based on lane_k.
            s = m.const_wire(0, width=seq_w)
            d = m.const_wire(0, width=128)
            for lane in range(4):
                is_lane = lane_k.eq(lane)
                s = is_lane.select(exp_seq[lane].out(), s)
                d = is_lane.select(lane_data0[lane], d)
            commit_seq_slots[k] = s
            commit_data_slots[k] = d

        _hist_shift_insert(hist, k=commit_cnt, seqs=commit_seq_slots, datas=commit_data_slots)

    # ---- bkpr update (after shadow counts) ----
    with m.scope("BKPR"):
        # Update shadow counts for each issue queue: cnt += push - pop.
        # Note: we use the queue fires as the source of truth.
        bkpr_next = m.const_wire(0, width=1)
        for lane in range(4):
            push_i = push_fire[lane].zext(width=16)
            pop_i = lane_pop[lane].zext(width=16)
            cnt_next = shadow_cnt[lane].out() + push_i - pop_i
            shadow_cnt[lane].set(cnt_next)

            # Assert when count is close to full (leave 2 entries of slack).
            near_full = cnt_next.uge(lane_q_depth - 2)
            bkpr_next = bkpr_next | near_full
        bkpr_r.set(bkpr_next)

    # ---- outputs ----
    m.output("pkt_in_bkpr", bkpr_r.out())
    for i in range(4):
        m.output(f"lane{i}_pkt_out_vld", out_vld_r[i].out())
        m.output(f"lane{i}_pkt_out_data", out_data_r[i].out())

    for e in range(total_eng):
        m.output(f"fwd{e}_pkt_data_vld", fwd_vld[e])
        m.output(f"fwd{e}_pkt_data", fwd_data[e])
        m.output(f"fwd{e}_pkt_lat", fwd_lat[e])
        m.output(f"fwd{e}_pkt_dp_vld", fwd_dp_vld[e])
        m.output(f"fwd{e}_pkt_dp_data", fwd_dp_data[e])


def build(
    m: Circuit,
    ENG_PER_LANE: int = 2,
    LANE_Q_DEPTH: int = 32,
    ENG_Q_DEPTH: int = 8,
    ROB_DEPTH: int = 16,
    SEQ_W: int = 16,
    WHEEL: int = 8,
    HIST_DEPTH: int = 8,
) -> None:
    # Wrapper kept tiny so the AST/JIT compiler executes the implementation as Python.
    _build_fastfwd(
        m,
        ENG_PER_LANE=ENG_PER_LANE,
        LANE_Q_DEPTH=LANE_Q_DEPTH,
        ENG_Q_DEPTH=ENG_Q_DEPTH,
        ROB_DEPTH=ROB_DEPTH,
        SEQ_W=SEQ_W,
        WHEEL=WHEEL,
        HIST_DEPTH=HIST_DEPTH,
    )


# Stable module name for codegen.
build.__pycircuit_name__ = "FastFwd"
