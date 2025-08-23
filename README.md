# candyCrush

Simple match-3 puzzle game built with SDL2.

## Controls

* **Select candy** – left-click a candy to highlight it.
* **Swap** – click a highlighted candy's horizontally or vertically adjacent neighbor to attempt a swap.

## Rules

* Swapping two adjacent candies that forms a chain of **three or more** identical candies removes them from the board.
* Each removed candy is worth **10 points**.
* Candies above fall into empty spaces and new candies spawn from the top to refill the grid.
* The game continues as long as there are valid moves available.

## Building

```sh
make
./candycrush
```

The game depends on `SDL2`, `SDL2_image`, `SDL2_mixer`, and `SDL2_ttf`.
