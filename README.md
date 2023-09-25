# Obsidian
An UCI chess engine written in c++


### Search algorithm

* Iterative deepening with aspiration windows
* Butterfly history
* Quiescence search
* Razoring
* Null move pruning
* Internal iterative reductions (IIR)
* Reverse futility pruning
* Late move reductions
* Check extension


### Evaluation

Obsidian evaluates position with a neural network, the architecture is 2x(768->384)->1 and is trained on 500M positions as of now.
Trivial endgames are evaluated with a special function.


### To do

* Make the code cleaner
* Add move which give check to quiescence search
* Implement more pruning techniques


### Credits
* The UCI structure and the Static Exchange Evaluation (SEE) are stockfish-like
* The NNUE of obsidian is trained with https://github.com/jw1912/bullet
