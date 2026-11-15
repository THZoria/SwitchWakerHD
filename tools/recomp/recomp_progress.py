"""Optional embedding progress; ordinary desktop recompilation stays silent."""


def report(owner, stage, current, total):
    callback = getattr(owner, "progress_callback", None)
    if callback is None:
        return
    total = max(1, total)
    current = min(current, total)
    state = (stage, current * 100 // total)
    if state != getattr(owner, "_last_progress", None):
        owner._last_progress = state
        callback(stage, current, total)


def tracked(owner, stage, items):
    total = len(items)
    report(owner, stage, 0, total)
    for index, item in enumerate(items):
        yield item
        report(owner, stage, index + 1, total)
    report(owner, stage, max(1, total), max(1, total))


class WorkProgress:
    """Count fixed-size batches without predicting fixpoint convergence.

    Newly scheduled analyses remain in their original queue order. A new batch
    is announced once the previous batch's number of analyses has completed.
    """
    def __init__(self, owner, stage):
        self.owner, self.stage = owner, stage
        self.batch = self.current = self.total = 0

    def begin(self, pending):
        if self.current == self.total:
            self.batch += 1
            self.current, self.total = 0, max(1, pending)
        report(self.owner, f"{self.stage} - lot {self.batch}", self.current, self.total)

    def advance(self, remaining):
        self.current += 1
        report(self.owner, f"{self.stage} - lot {self.batch}",
               self.total if not remaining else self.current, self.total)
