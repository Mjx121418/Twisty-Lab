#!/usr/bin/env python3
"""Compatibility entry point; implementation lives in tools.authoring.models."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.authoring.compat import forward
forward('melty_spherical', __name__)
