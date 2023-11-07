# Obsidian
A top tier UCI chess engine written in c++


### Neural network

Obsidian evaluates positions with a neural network, the architecture is 2x(768->1024)->1 and is trained on 2.8B positions as of now.
Trivial endgames are evaluated with a special function instead.


### Credits
* To Witek902, for letting me in his OpenBench instance, allowing me to use massive hardware for my tests
* The NNUE of obsidian is trained with https://github.com/jw1912/bullet
