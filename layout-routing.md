# ERD Layout / Routing Strategy

## 목표

Django 모델 ERD는 직사각형 테이블 노드와 방향성 FK/M2M 관계가 많은 그래프다. 사용자가 기대하는 품질은 다음 기준으로 본다.

- 테이블 노드가 겹치지 않고 충분한 읽기 간격을 가진다.
- 주요 관계 방향이 한눈에 보이도록 계층이 유지된다.
- edge 교차를 줄이고, 같은 테이블에서 나가는 edge가 한 포트에 뭉치지 않는다.
- edge가 unrelated table box 위를 통과하지 않는다.
- 드래그와 뷰 조작은 즉각 반응하고, 비싼 계산은 레이아웃 재계산 시점에만 수행한다.

## 선택한 기본 알고리즘

기본 ERD 레이아웃은 `layered + barycenter + orthogonal routing + port/lane assignment` 조합을 우선한다.

1. Layer assignment

   관계 방향을 기준으로 노드를 rank/layer에 배치한다. source에서 target으로 흐르는 모델 의존성을 최대한 같은 방향으로 펼치면, 사용자가 FK 흐름을 추적하기 쉽고 edge가 무작위 각도로 흩어지지 않는다.

2. Barycenter ordering

   각 layer 내부 순서를 인접 layer의 연결 위치 평균값으로 반복 정렬한다. 이 방식은 Sugiyama-style layered drawing에서 널리 쓰이는 crossing reduction 휴리스틱이고, 구현 비용 대비 효과가 좋다.

3. Limited transpose pass

   barycenter sweep 이후 인접 노드 swap으로 실제 crossing count가 줄어드는 경우만 반영한다. 작은/중간 그래프에서는 품질을 높이고, 큰 그래프에서는 반복 횟수를 제한해 계산량을 통제한다.

4. Coordinate assignment

   layer gap과 row gap을 테이블 크기 및 사용자의 node spacing 설정에 맞춰 계산한다. 이후 최종 collision relaxation은 보정 단계로만 사용하고, 레이아웃의 큰 구조를 깨지 않도록 axis lock을 유지한다.

5. Port assignment

   edge endpoint를 테이블 중앙 1점에 모으지 않고, side별 port index/count로 분산한다. shared source/target 관계가 많은 ERD에서 edge가 한 줄로 겹쳐 보이는 현상을 줄인다.

6. Lane assignment

   같은 방향으로 진행하는 orthogonal trunk는 span overlap을 기준으로 lane을 분리한다. reverse/derived edge는 바깥쪽 lane을 선호시켜 정방향 edge와 시각적으로 구분한다.

7. Orthogonal routing

   edge는 수평/수직 segment로 라우팅한다. 테이블형 노드에서는 사선보다 orthogonal path가 box 경계와 관계 방향을 읽기 쉽고, port/lane assignment와 결합하기도 쉽다.

8. Obstacle-aware fallback

   기본 orthogonal path가 unrelated table box를 관통하면 vertical/horizontal channel 후보와 grid routing 후보를 점수화해 우회 경로를 선택한다. 큰 그래프에서는 grid 후보를 제한해 최악 계산량을 막는다.

## 보조 레이아웃의 역할

- `hierarchical`, `neural`, `flow`는 같은 layered pipeline을 공유하는 1차 ERD 레이아웃으로 둔다.
- `graph`는 관계 중심 overview 용도다. 작은 그래프에서는 force-directed가 가능하지만, 큰 그래프에서는 concentric/fast layout으로 전환해 계산 폭증을 피한다.
- `radial`, `circular`, `clustered`는 탐색/그룹 보기 용도의 보조 레이아웃이다. edge crossing 최소화가 주목적인 기본 ERD 보기로 쓰지 않는다.

## 성능 정책

- layout option 변경은 즉시 재계산하지 않고 Refresh 이후 적용한다.
- 드래그 중에는 전체 레이아웃과 edge routing을 다시 계산하지 않는다. preview 렌더링만 수행하고 drop 시점에 필요한 계산을 수행한다.
- layout variant는 lazy cache로 계산한다. 사용자가 선택하지 않은 레이아웃을 초기 렌더링에서 모두 계산하지 않는다.
- barycenter sweep, transpose, collision relaxation, grid routing은 노드/edge 수에 따라 반복 횟수와 후보 수를 제한한다.
- edge routing은 port/lane assignment 이후 obstacle-aware 후보 중 최저 비용 경로를 선택하되, dense graph에서는 비싼 grid search를 건너뛸 수 있다.

## 구현 우선순위

1. `hierarchical` 기본 경로를 layered+barycenter 기반으로 정렬한다.
2. `flow`와 `neural`은 orientation만 다른 같은 pipeline으로 유지한다.
3. visible/catalog edge routing은 port assignment, lane assignment, orthogonal route, obstacle-aware scoring 순서를 유지한다.
4. force-directed는 기본 경로가 아니라 `graph` 보조 보기 안에서만 제한적으로 사용한다.
5. layout setting 변경은 pending 상태로 두고 Refresh 전까지 applied layout에 반영하지 않는다.
