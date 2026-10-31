"""Publish validated trees with exception rollback; generation never touches live outputs."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import shutil
import tempfile
from .project import require


@contextmanager
def lock(path):
    try:
        descriptor = os.open(path, os.O_WRONLY|os.O_CREAT|os.O_EXCL, 0o600)
    except FileExistsError as error:
        raise RuntimeError(f'Another authoring build owns {path}.') from error
    try:
        with os.fdopen(descriptor,'w') as stream: json.dump({'pid':os.getpid()},stream)
        yield
    finally:
        Path(path).unlink(missing_ok=True)


def publish(entries, backup_parent):
    """Entries are (staged path, destination). All must share the destination filesystem."""
    require(len({str(destination) for _,destination in entries})==len(entries), 'Duplicate publication destination.')
    for staged,destination in entries:
        require(staged.exists() and not staged.is_symlink(), f'Missing staged output {staged}.')
        require(not destination.is_symlink(), f'Publication cannot replace a symlink: {destination}.')
        require(destination.parent.is_dir(), f'Missing output parent {destination.parent}.')
    with tempfile.TemporaryDirectory(prefix='.twisty-previous-',dir=backup_parent) as previous:
        changes=[]
        try:
            for i,(staged,destination) in enumerate(entries):
                backup=Path(previous)/str(i)
                existed=destination.exists()
                if existed: os.replace(destination,backup)
                changes.append((destination,backup,existed,False))
                os.replace(staged,destination)
                changes[-1]=(destination,backup,existed,True)
        except BaseException:
            for destination,backup,existed,published in reversed(changes):
                if published:
                    if destination.is_dir(): shutil.rmtree(destination)
                    else: destination.unlink()
                if existed: os.replace(backup,destination)
            raise
