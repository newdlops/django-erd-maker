import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../..");

test("joint label solver crosses a three-node barrier and agrees with exhaustive search", (t) => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), "erd-label-test-"));
  t.after(() => fs.rmSync(directory, { recursive: true, force: true }));
  const source = path.join(directory, "test.cpp"), binary = path.join(directory, "test");
  fs.writeFileSync(source, `
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <functional>
#include <random>
#include <stdexcept>
#include <vector>
#include ${JSON.stringify(path.join(root, "scripts/erd-poc/pairwise_label_optimizer.h"))}
#include ${JSON.stringify(path.join(root, "scripts/erd-poc/finite_factor_optimizer.h"))}
int main() {
  PairwiseLabels barrier;
  barrier.unary={{1,0},{1,0},{1,0}};
  barrier.pairs.resize(9);
  for(int i=0;i<3;i++)for(int j=i+1;j<3;j++)barrier.pairs[i*3+j]={0,10,10,0};
  assert(barrier.energy({0,0,0})==3);
  for(int bits=1;bits<7;bits++)assert(barrier.energy({bits&1,(bits>>1)&1,(bits>>2)&1})>3);
  const auto solution=solvePairwiseLabels(barrier,1,42,[]{return false;});
  assert(solution.energy==0&&solution.tripleWins==1);
  assert(solution.labels==std::vector<int>({1,1,1}));

  PairwiseLabels irregular;
  irregular.unary={{4,1},{7,2,3},{3,4,1,2}};
  irregular.pairs.resize(9);
  for(int i=0;i<3;i++)for(int j=i+1;j<3;j++)
    for(int a=0;a<int(irregular.unary[i].size());a++)for(int b=0;b<int(irregular.unary[j].size());b++)
      irregular.pairs[i*3+j].push_back(((a+2)*(b+3)+i*5+j*7)%11);
  long minimum=1000000000;
  for(int a=0;a<2;a++)for(int b=0;b<3;b++)for(int c=0;c<4;c++) {
    long exact=irregular.unary[0][a]+irregular.unary[1][b]+irregular.unary[2][c]
      +irregular.pairs[1][a*3+b]+irregular.pairs[2][a*4+c]+irregular.pairs[5][b*4+c];
    assert(exact==irregular.energy({a,b,c}));minimum=std::min(minimum,exact);
  }
  assert(solvePairwiseLabels(irregular,1,77,[]{return false;}).energy==minimum);
  const auto interrupted=solvePairwiseLabels(irregular,10000,77,[]{return true;});
  assert(interrupted.labels==std::vector<int>({0,0,0}));
  assert(interrupted.energy==irregular.energy({0,0,0}));

  PairwiseLabels large=barrier;
  for(auto& unary:large.unary)unary={1000000000,1000000000};
  assert(large.energy({0,0,0})==3000000000L);

  FiniteLabelFactors four;
  four.domains={2,2,2,2};four.incident={{0},{0},{0},{0}};
  DenseLabelFactor crossing;
  crossing.variables={0,1,2,3};crossing.strides={8,4,2,1};crossing.values.assign(16,9);
  crossing.values[0]=6;crossing.values[15]=0;four.factors={crossing};
  auto fourSolution=solveFiniteFactors(four,1,42,[]{return false;});
  assert(fourSolution.energy==0&&fourSolution.labels==std::vector<int>({1,1,1,1}));

  // A cyclic five-card exchange has no legal partial move of <=4 cards.
  // Soft intermediate spacing costs must never replace the feasible output.
  FiniteLabelFactors exchange;
  exchange.domains.assign(5,2);exchange.incident.resize(5);
  const auto add=[&](DenseLabelFactor f) {
    for(int n:f.variables)exchange.incident[n].push_back(exchange.factors.size());
    exchange.factors.push_back(f);
  };
  for(int n=0;n<5;n++)add({{n},{1},{3,0},{}});
  for(int n=0;n<5;n++)add({{n,(n+1)%5},{2,1},{0,0,0,0},{0,1,1,0}});
  assert(exchange.energy({0,0,0,0,0})==15);
  assert(exchange.energy({1,0,0,0,0},0)==12);
  assert(exchange.spacingCount({1,0,0,0,0})==2);
  assert(exchange.energy({1,0,0,0,0})==2000000012L);
  assert(solveFiniteFactors(exchange,1000,42,[]{return false;}).energy==15);
  auto relaxed=solveFiniteFactors(exchange,1000,42,[]{return false;},true);
  assert(relaxed.energy==0&&exchange.spacingCount(relaxed.labels)==0);
  auto expired=solveFiniteFactors(exchange,1000,42,[]{return true;},true);
  assert(expired.energy==15&&expired.labels==std::vector<int>(5,0));
  return 0;
}
`);
  const compile = spawnSync(process.env.CXX || "c++", ["-O2", "-std=c++17", "-Wall", "-Wextra", source, "-o", binary],
    { encoding: "utf8", timeout: 60_000 });
  assert.equal(compile.status, 0, compile.stderr || String(compile.error));
  const run = spawnSync(binary, [], { encoding: "utf8", timeout: 2000 });
  assert.equal(run.status, 0, run.stderr || String(run.error));
});
