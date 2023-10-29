# Obsidian
An UCI chess engine written in c++


### Evaluation

Obsidian evaluates position with a neural network, the architecture is 2x(768->384)->1 and is trained on 500M positions as of now.
Trivial endgames are evaluated with a special function.


### To do

* Add move which give check to quiescence search
* Implement more pruning techniques


### Credits
* To Witek902, for letting me in his OpenBench instance and allowing me to use a lot of hardware for my tests
* The UCI structure and the Static Exchange Evaluation (SEE) are stockfish-like
* The NNUE of obsidian is trained with https://github.com/jw1912/bullet
