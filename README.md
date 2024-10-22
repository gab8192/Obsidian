# Obsidian
A top tier UCI chess engine written in c++, that I started developing in april 2023.

As of 18 July 2024, Obsidian is the 3rd strongest engine at 10+1s, after Stockfish and Torch.
(Ipmanchess rating 3554)


## Building
```
git clone https://github.com/gab8192/Obsidian
cd Obsidian
make nopgo build=ARCH
```
ARCH choice: native, sse2, ssse3, avx2, avx512. You can also do sse2-pext, ssse3-pext, avx2-pext. `native` is recommended however.
You can remove the `nopgo` flag to enable profile guided optimization.


## Neural network
Obsidian evaluates positions with a neural network trained on Lc0 data.


## Credits
* To Styxdoto (or Styx), he has an incredible machine with 128 threads and he has donated CPU time
* To Witek902, for letting me in his OpenBench instance, allowing me to use massive hardware for my tests
* To fireandice, for training the neural network of Obsidian 9.0
* The neural network of obsidian is trained with https://github.com/jw1912/bullet
