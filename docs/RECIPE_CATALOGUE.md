# Recipe Catalogue

`app/assets/data/recipes.json` is the authored source of truth for every craftable item. It is
loaded once before gameplay, validated against `blocks.json`, then included in `core.vpk` by the
normal asset bundle process. Content errors stop gameplay with the recipe id and field in the
error message.

```json
{
  "recipes": [
    {
      "id": "recipe_planks",
      "icon": "planks",
      "category": "construction",
      "ingredients": [{ "item": "wood_log", "count": 1 }],
      "output_item": "planks",
      "output_count": 4
    }
  ]
}
```

`id` is a unique stable identifier used by the player UI action. `icon` identifies a bundled
player UI item icon. `category` must be one of `construction`, `food`, `tools`, `furniture`, or
`smelters`. Every `item` and `output_item` must resolve to an item id in `blocks.json`. Counts
are positive integers bounded by the inventory's total capacity. An ingredient may appear only
once in a recipe.

Crafting is authoritative and atomic: inputs are removed and output is added only when every
input exists and the full output fits after removal. The browser renders the catalogue snapshot
and submits the selected stable `id`; it cannot mutate inventory directly.

## Tool Catalogue

`app/assets/data/tools.json` contains one entry per inventory-only tool item. Every entry has a
unique `id`, an `item` id from `blocks.json`, a `kind` (`axe`, `pickaxe`, `shovel`,
`sledgehammer`, `sword`, or `hoe`), `tier`, `max_durability`, `base_damage`,
`swing_interval_seconds`, and non-empty `mining_speed_multipliers` object keyed by block
`metadata.tool_tag`. Invalid ids, tool kinds, counts, timings, or multipliers prevent entering
gameplay with the precise tool and field named. The catalogue is resolved once at startup; fixed
simulation performs numeric item lookup only.

Tool durability is stored per inventory stack. A zero value denotes an unused, full tool and is
initialized on its first successful effect. Mining and tilling consume exactly one point only
after the authoritative world mutation succeeds; a final use removes the tool stack.