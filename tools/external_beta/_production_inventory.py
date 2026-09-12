"""Version-aware inventory admission for producer definitions and verification."""
from tools.voicebank_script_generator import production_assignments, validate_inventory
from tools.voicebank_script_generator.draft_inventory import draft_production_assignments, validate_draft_inventory


def style_owned(inventory):
    return isinstance(inventory, dict) and type(inventory.get("schemaVersion")) is int and inventory["schemaVersion"] == 2


def inventory_errors(inventory):
    return validate_draft_inventory(inventory) if style_owned(inventory) else validate_inventory(inventory)


def producer_assignments(inventory):
    if not style_owned(inventory):
        return production_assignments(inventory)
    # Language belongs to the immutable producer workspace, not each row.
    return [{key: value for key, value in row.items() if key != "language"}
            for row in draft_production_assignments(inventory)]
