# Obsidian
An UCI chess engine written in c++

### Search algorithm

* Iterative deepening with aspiration windows
* Very simple negamax
* Very simple move ordering
* Quiescence search
* Razoring
* Null move pruning
* Internal iterative reductions (IIR)
* Check extension

### Evaluation

Obsidian evaluates position with a neural network, the architecture is 2x(768->256)->1 and is trained on 100M positions as of now.
Trivial endgames are evaluated with a special function.

### To do
* Write UCI and Static Exchange Evaluation (SEE) from scratch. They are partially copied from stockfish. Yeah that sucks
* Make the code cleaner
* Add move which give check to quiescence search
* Implement more pruning techniques
