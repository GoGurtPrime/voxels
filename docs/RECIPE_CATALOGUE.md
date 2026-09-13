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