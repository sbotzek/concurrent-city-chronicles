# Concurrent City Chronicles

This project is a simple city simulator for comparing concurrency designs and performance across languages.

## Features
- ASCII city grid
- Pops with needs: food, work, play, sleep
- Four location types: home, job, park, diner
- Each tile holds at most one pop
- Pops move to fulfill needs

## Running
Each version will run in slightly different ways.  However, there should be some standard arguments they take:

-vm: 'visual mode', default, runs and displays the simulation as normal
-bm: 'benchmark mode', eliminates all IO and no sleeps
