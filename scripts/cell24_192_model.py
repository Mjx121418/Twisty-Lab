"""Ordinary 45-degree 24-cell: 192 movable pieces and 24 unmarked centers."""
from cell24_model import Model as FullModel

ID='cell24-192'


class Model(FullModel):
    def __init__(self):
        super().__init__()
        self.domains=[d for d in self.domains if d['prototype'] in ('center','face','edge')]
        assert sum(len(d['pieces']) for d in self.domains)==216
        assert sum(len(d['poses']) for d in self.domains)==792
