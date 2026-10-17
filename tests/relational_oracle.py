"""Direct source semantics, deliberately independent of the C++ predicate IR.

Enumerate only tiny toy states/tuples. No compiler output is used to evaluate R,
transports, invariants, or witnesses. The same cases can also check WASM.
"""
import argparse
import copy
import itertools
import json
from pathlib import Path
import random
import subprocess
import tempfile


class Oracle:
    def __init__(self, source):
        self.source = source
        self.domains = source["domains"]
        self.placements, self.pieces, self.initial, self.events = [], [], [], []
        self.allowed = []
        for domain in self.domains:
            slots = [domain["id"] + "/" + q for q in domain["placements"]]
            self.placements.extend(slots)
            for piece in domain["pieces"]:
                self.pieces.append(piece["id"])
                self.allowed.append(set(slots))
                self.initial.append(domain["id"] + "/" + piece["initial"])
            for move in domain["moves"]:
                event = move.get("event", domain["id"] + "/" + move["id"])
                if event not in self.events:
                    self.events.append(event)
        self.choice_values = [["idle"] + [m["id"] for m in d["moves"]] for d in self.domains]

    def evaluate(self, expr, state, request, choices):
        if isinstance(expr, bool):
            return expr
        op, args = next(iter(expr.items()))
        sub = lambda x: self.evaluate(x, state, request, choices)
        if op == "ref":
            return sub(self.source["predicates"][args])
        if op == "at":
            return state[self.pieces.index(args["piece"])] in args["placements"]
        if op == "chosen":
            return choices[[d["id"] for d in self.domains].index(args["domain"])] in args["moves"]
        if op == "request":
            return request in args
        if op == "not":
            return not sub(args)
        if op == "all":
            return all(sub(x) for x in args)
        if op == "any":
            return any(sub(x) for x in args)
        if op == "implies":
            return not sub(args[0]) or sub(args[1])
        if op == "iff":
            return sub(args[0]) == sub(args[1])
        if op == "count":
            return args["min"] <= sum(sub(x) for x in args["of"]) <= args["max"]
        if op == "occupiedBy":
            return any(state[self.pieces.index(p)] == args["placement"] for p in args["pieces"])
        raise AssertionError(op)

    def valid(self, state):
        return all(q in allowed for q, allowed in zip(state, self.allowed)) and self.evaluate(
            self.source.get("invariant", True), state, "", [])

    def plan(self, state, request, choices):
        permitted = self.evaluate(request["relation"], state, request["id"], choices)
        permitted &= all(self.evaluate(r["predicate"], state, request["id"], choices)
                         for r in self.source.get("relations", []))
        after, actions = list(state), []
        for i, domain in enumerate(self.domains):
            if choices[i] == "idle":
                continue
            move = next(m for m in domain["moves"] if m["id"] == choices[i])
            permitted &= self.evaluate(move.get("requires", True), state, request["id"], choices)
            for piece in domain["pieces"]:
                if piece["id"] not in move.get("pieces", [p["id"] for p in domain["pieces"]]):
                    continue
                p = self.pieces.index(piece["id"])
                local = state[p][len(domain["id"]) + 1:]
                if local not in move["support"]:
                    continue
                after[p] = domain["id"] + "/" + move["transport"][local]
                event = move.get("event", domain["id"] + "/" + move["id"])
                actions.append({"piece": p, "from": self.placements.index(state[p]),
                                "to": self.placements.index(after[p]), "event": self.events.index(event)})
        if not permitted:
            return {"status": "Rejected", "reason": "relation"}
        if not self.valid(after):
            return {"status": "Rejected", "reason": "target-invariant"}
        return {"status": "Permitted", "transition": {
            "request": request["id"], "choices": list(choices),
            "before": [self.placements.index(q) for q in state],
            "after": [self.placements.index(q) for q in after], "actions": actions}}

    def cases(self):
        queries, expected = [], []
        counts = set()
        for state in itertools.product(self.placements, repeat=len(self.pieces)):
            numeric = [self.placements.index(q) for q in state]
            valid = self.valid(state)
            queries.append({"op": "validate", "state": numeric})
            expected.append({"status": "Valid" if valid else "Invalid"})
            if not valid:
                continue
            queries.append({"op": "goal", "state": numeric})
            solved = self.evaluate(self.source["goal"], state, "", []) if "goal" in self.source else list(state) == self.initial
            expected.append({"status": "Evaluated", "solved": solved})
            for request in self.source["requests"]:
                count = 0
                for choices in itertools.product(*self.choice_values):
                    queries.append({"op": "plan", "state": numeric, "request": request["id"], "choices": choices})
                    result = self.plan(state, request, choices)
                    expected.append(result)
                    count += result["status"] == "Permitted"
                counts.add(min(count, 2))
        return queries, expected, counts


def formulas(seed):
    rng = random.Random(seed)
    leaves = [True, False, {"request": ["fuzz"]}]
    for domain in ("driver", "follower", "latch"):
        leaves.append({"chosen": {"domain": domain, "moves": ["turn"]}})
        leaves.append({"at": {"piece": domain + "-piece", "placements": [domain + "/0"]}})
    def make(depth):
        if depth == 0 or rng.random() < .25:
            return copy.deepcopy(rng.choice(leaves))
        op = rng.choice(["not", "all", "any", "implies", "iff", "count"])
        if op == "not":
            return {op: make(depth - 1)}
        children = [make(depth - 1) for _ in range(2 if op in ("implies", "iff") else rng.randrange(4))]
        if op == "count":
            # Includes empty, impossible, and repeated equivalent terms.
            if children and rng.random() < .5:
                children.append(copy.deepcopy(children[0]))
            lower, upper = sorted([rng.randrange(5), rng.randrange(5)])
            return {op: {"min": lower, "max": upper, "of": children}}
        return {op: children}
    return [make(4) for _ in range(32)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("fixture")
    parser.add_argument("--emit")
    args = parser.parse_args()
    base = json.loads(Path(args.fixture).read_text())
    sources = [base]
    for expression in formulas(20261017):
        source = copy.deepcopy(base)
        source["requests"] = [{"id": "fuzz", "relation": expression}]
        sources.append(source)
    # More than one piece in a local catalog, dual occupancy and exclusion.
    occupancy = copy.deepcopy(base)
    occupancy["domains"][0]["pieces"].append({"id": "other-driver", "initial": "1"})
    occupancy["invariant"] = {"all": [occupancy["invariant"], {"count": {"min": 1, "max": 1, "of": [
        {"at": {"piece": p, "placements": ["driver/0"]}} for p in ["driver-piece", "other-driver"]]}}]}
    occupancy["requests"] = [{"id": "occupancy", "relation": {"occupiedBy": {
        "placement": "driver/1", "pieces": ["driver-piece", "other-driver"]}}}]
    # A move limited to one identity can now violate that target exclusion.
    occupancy["domains"][0]["moves"][0]["pieces"] = ["driver-piece"]
    sources.append(occupancy)
    suites, total, cardinalities = [], 0, set()
    with tempfile.TemporaryDirectory(prefix="twisty-relational-oracle-") as directory:
        for i, source in enumerate(sources):
            oracle = Oracle(source)
            path = Path(directory) / f"case-{i}.json"
            text = json.dumps(source)
            path.write_text(text)
            preview = json.loads(subprocess.check_output([args.executable, str(path), "--inspect"], text=True, timeout=30))
            assert preview["bindings"]["pieces"] == oracle.pieces
            assert preview["bindings"]["placements"] == oracle.placements
            assert preview["bindings"]["events"] == oracle.events
            assert preview["bindings"]["choiceValues"] == oracle.choice_values
            assert preview["program"]["placementCount"] == sum(len(d["placements"]) for d in source["domains"])
            # Domains and strings are compiler metadata, absent from execution data.
            assert "domain" not in json.dumps(preview["program"]).lower()
            queries, expected, counts = oracle.cases()
            cardinalities.update(counts)
            result = subprocess.run([args.executable, str(path)], input="\n".join(map(json.dumps, queries)) + "\n",
                                    capture_output=True, text=True, timeout=45, check=True)
            actual = [json.loads(line) for line in result.stdout.splitlines()]
            assert len(actual) == len(expected), (i, result.stderr)
            for query, want, got in zip(queries, expected, actual):
                assert got == want, (i, query, want, got)
            total += len(queries)
            if args.emit:
                suites.append({"source": text, "queries": queries, "expected": expected, "preview": preview})
    assert cardinalities == {0, 1, 2}, "Examples must cover zero, one, and multiple completions"
    if args.emit:
        Path(args.emit).write_text(json.dumps(suites, separators=(",", ":")))
    print(f"Independent source oracle: {total} exact state/tuple/witness checks across {len(sources)} models")


if __name__ == "__main__":
    main()
