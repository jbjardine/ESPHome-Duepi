"""Use the official Home Assistant Core 2026.10 test fixtures."""

from pathlib import Path

import pytest
from homeassistant import loader

pytest_plugins = ["tests.conftest", "pytest_asyncio.plugin"]


@pytest.fixture
def duepi_custom_components(hass):
    """Add this checkout's integration to the official test package path."""
    import custom_components

    integration_root = str(Path(__file__).parents[2] / "custom_components")
    custom_components.__path__[:] = [integration_root]
    hass.data.pop(loader.DATA_CUSTOM_COMPONENTS, None)
