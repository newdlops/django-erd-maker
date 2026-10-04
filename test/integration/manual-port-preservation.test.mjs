import assert from "node:assert/strict";
import { createRequire } from "node:module";
import test from "node:test";
import vm from "node:vm";

const require = createRequire(import.meta.url);
const { getBrowserLayoutSource } = require("../../out/webview/interaction/runtime/browserLayoutSource.js");
const runtime = vm.runInNewContext(getBrowserLayoutSource() +
  "; ({getStaticOrCatalogEdgePaths, getStaticOrLiveEdgePath})");
const plain = value => JSON.parse(JSON.stringify(value));
const table = (x,y,width,height) => ({basePosition:{x,y},width,height});
const entry = (id,source,target,points) => ({
  meta:{edgeId:id,sourceModelId:id + ".source",targetModelId:id + ".target",points,preserveRouteEndpoints:true},
  sourceTable:source,targetTable:target,
  sourcePosition:{...source.basePosition},targetPosition:{...target.basePosition},
});
const coupon = () => entry("coupon", table(15122.62,10177.39,303,82),
  table(13288.89,8677.70,380,434), "15122.62,10177.39 13668.89,9036.79");

test("Captain coupon drag retains the unmoved Company port and all static neighbors", () => {
  const moved = coupon();
  moved.sourcePosition.y = 10142.26;
  const neighbor = entry("neighbor", table(18000,9000,240,74), moved.targetTable,
    "18000,9000 13668.89,8990");
  const routes = plain(runtime.getStaticOrCatalogEdgePaths([moved,neighbor]));
  assert.deepEqual(routes[0].points, [{x:15122.62,y:10142.26},{x:13668.89,y:9036.79}]);
  assert.deepEqual(routes[1].points, [{x:18000,y:9000},{x:13668.89,y:8990}]);
});

test("rigid manual translation preserves both port offsets and the straight path", () => {
  const moved = coupon();
  for (const position of [moved.sourcePosition,moved.targetPosition]) {position.x+=33;position.y-=40;}
  assert.deepEqual(plain(runtime.getStaticOrLiveEdgePath(moved)),
    [{x:15155.62,y:10137.39},{x:13701.89,y:8996.79}]);
});

test("a port facing into its own card is repaired without changing the valid peer port", () => {
  const moved = entry("flip", table(400,-200,100,100), table(0,0,100,100), "400,-150 0,0");
  moved.sourcePosition.x=-400;
  const route=plain(runtime.getStaticOrLiveEdgePath(moved));
  assert.equal(route.length,2);
  assert.equal(route[0].x,-300);
  assert.ok(route[0].y>=-200 && route[0].y<=-100);
  assert.deepEqual(route[1],{x:0,y:0});
});

test("moving a card across its peer reconnects both outward-facing boundaries", () => {
  const moved = entry("across", table(400,0,100,100), table(0,0,100,100), "400,50 100,50");
  moved.sourcePosition.x=-400;
  assert.deepEqual(plain(runtime.getStaticOrCatalogEdgePaths([moved]))[0].points,
    [{x:-300,y:50},{x:0,y:50}]);
});

test("returning a card to its base restores the exact original route", () => {
  const moved=coupon();
  moved.sourcePosition.y+=100;
  runtime.getStaticOrLiveEdgePath(moved);
  moved.sourcePosition={...moved.sourceTable.basePosition};
  assert.deepEqual(plain(runtime.getStaticOrLiveEdgePath(moved)),
    [{x:15122.62,y:10177.39},{x:13668.89,y:9036.79}]);
});
