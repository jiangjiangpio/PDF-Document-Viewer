#include "core.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
using namespace paper;
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x);             \
    } while (false)
static bool near(double a, double b) {
    return std::abs(a - b) < 1e-6;
}
static Stroke stroke(int page, std::initializer_list<Point> p) {
    Stroke s;
    s.page = page;
    s.points = p;
    return s;
}
int main() {
    int passed = 0, failed = 0;
    auto test = [&](const char *name, auto body) {
        try {
            body();
            ++passed;
            std::cout << "PASS " << name << '\n';
        } catch (const std::exception &e) {
            ++failed;
            std::cerr << "FAIL " << name << ": " << e.what() << '\n';
        }
    };
    test("all rotation coordinate roundtrips", [] {
        for (int r = 0; r < 4; ++r) {
            PageTransform t({612, 792}, r);
            for (Point p : std::vector<Point>{{0, 0}, {612, 792}, {111.4, 555.2}}) {
                auto q = t.toPage(t.toView(p));
                CHECK(near(q.x, p.x));
                CHECK(near(q.y, p.y));
            }
        }
    });
    test("annotation coordinates survive zoom and rotation", [] {
        ViewportController v;
        v.resize(900, 650);
        v.setPages({{612, 792}, {500, 800}});
        for (int r = 0; r < 4; ++r) {
            v.setRotation(r);
            v.setZoom(1.4, {450, 325});
            v.goTo(0);
            Point original{200, 250};
            auto screen = v.screenPoint(0, original);
            auto hit = v.pageAt(screen);
            CHECK(hit.has_value());
            CHECK(hit->first == 0);
            CHECK(near(hit->second.x, original.x));
            CHECK(near(hit->second.y, original.y));
        }
    });
    test("zoom preserves point beneath anchor", [] {
        ViewportController v;
        v.resize(500, 400);
        v.setPages({{1000, 1600}});
        v.setZoom(2, {250, 200});
        v.scrollY = 450;
        v.scrollX = 150;
        Point anchor{220, 180};
        auto before = v.pageAt(anchor);
        v.setZoom(2.4, anchor);
        auto after = v.pageAt(anchor);
        CHECK(before && after);
        CHECK(near(before->second.x, after->second.x));
        CHECK(near(before->second.y, after->second.y));
    });
    test("long documents only expose nearby pages", [] {
        ViewportController v;
        v.setPages(std::vector<Size>(1000, {612, 792}));
        v.goTo(700);
        auto visible = v.visible(100);
        CHECK(visible.size() < 6);
        CHECK(std::find(visible.begin(), visible.end(), 700) != visible.end());
    });
    test("resize keeps reading page", [] {
        ViewportController v;
        v.setPages(std::vector<Size>(40, {612, 792}));
        v.goTo(25);
        v.resize(1100, 700);
        CHECK(v.currentPage() == 25);
    });
    test("stroke hit testing includes segments", [] {
        auto s = stroke(0, {{0, 0}, {100, 100}});
        CHECK(s.hit({50, 50}, 1));
        CHECK(!s.hit({50, 80}, 1));
        CHECK(s.hit({0, 0}, 0));
    });
    test("undo redo and branching", [] {
        AnnotationDocument d;
        d.add(stroke(0, {{1, 2}, {3, 4}}));
        CHECK(d.page(0).size() == 1);
        CHECK(d.undo());
        CHECK(d.page(0).empty());
        CHECK(d.redo());
        CHECK(d.page(0).size() == 1);
        CHECK(d.undo());
        d.add(stroke(1, {{10, 20}}));
        CHECK(!d.redo());
        CHECK(d.page(1).size() == 1);
    });
    test("eraser transaction restores ordering", [] {
        AnnotationDocument d;
        for (int i = 0; i < 5; ++i)
            d.add(stroke(0, {{double(i * 20), 0}, {double(i * 20), 10}}));
        auto before = d.page(0);
        CHECK(d.eraseIds(0, {before[1]->id, before[3]->id}));
        CHECK(d.page(0).size() == 3);
        CHECK(d.undo());
        CHECK(d.page(0) == before);
        CHECK(d.redo());
        CHECK(d.page(0).size() == 3);
    });
    test("second finger cancels provisional ink", [] {
        int began = 0, appended = 0, committed = 0, cancelled = 0, pans = 0, pinches = 0;
        InputController input({[&](Point) { ++began; }, [&](Point) { ++appended; }, [&] { ++committed; },
                               [&] { ++cancelled; }, [&](Point) { ++pans; },
                               [&](Point, double, Point) { ++pinches; }});
        input.down(1, {10, 10}, true);
        input.move(1, {20, 20});
        input.down(2, {100, 100}, true);
        input.move(2, {120, 120});
        input.up(2, {120, 120});
        input.move(1, {30, 30});
        input.up(1, {30, 30});
        CHECK(began == 1);
        CHECK(appended == 1);
        CHECK(committed == 0);
        CHECK(cancelled == 1);
        CHECK(pinches == 1);
        CHECK(pans == 1);
        CHECK(!input.active());
    });
    test("mouse and finger share predictable stroke lifecycle", [] {
        int commits = 0, cancels = 0;
        InputController input({[](Point) {}, [](Point) {}, [&] { ++commits; }, [&] { ++cancels; },
                               [](Point) {}, [](Point, double, Point) {}});
        input.down(0, {0, 0}, true);
        input.up(0, {10, 10});
        input.up(0, {10, 10});
        CHECK(commits == 1);
        input.down(8, {0, 0}, true);
        input.cancel();
        input.up(8, {10, 10});
        CHECK(commits == 1);
        CHECK(cancels == 1);
    });
    test("reading drag never makes ink", [] {
        int begin = 0;
        Point total{};
        InputController input({[&](Point) { ++begin; }, [](Point) {}, [] {}, [] {},
                               [&](Point p) { total = total + p; }, [](Point, double, Point) {}});
        input.down(1, {10, 20}, false);
        input.move(1, {25, 60});
        input.up(1, {25, 60});
        CHECK(begin == 0);
        CHECK(total.x == 15 && total.y == 40);
    });
    test("pinch reports ratio and centroid translation", [] {
        double ratio = 0;
        Point anchor{}, pan{};
        InputController input({[](Point) {}, [](Point) {}, [] {}, [] {}, [](Point) {},
                               [&](Point a, double r, Point p) {
                                   anchor = a;
                                   ratio = r;
                                   pan = p;
                               }});
        input.down(1, {0, 0}, false);
        input.down(2, {100, 0}, false);
        input.move(2, {200, 0});
        CHECK(near(ratio, 2));
        CHECK(near(anchor.x, 50));
        CHECK(near(pan.x, 50));
    });
    test("tile cache obeys budget and LRU", [] {
        TileCache c(20);
        auto make = []() {
            auto t = std::make_shared<Tile>();
            t->pixels.resize(10);
            return t;
        };
        TileKey a{1, 0, 64, 0, 0, 0}, b{1, 0, 64, 0, 1, 0}, d{2, 0, 64, 0, 0, 0};
        c.put(a, make());
        c.put(b, make());
        CHECK(c.get(a));
        c.put(d, make());
        CHECK(!c.get(b));
        CHECK(c.get(a));
        CHECK(c.bytes() == 20);
        c.removeDocument(1);
        CHECK(c.bytes() == 10);
        CHECK(c.get(d));
    });
    test("oversized tiles cannot exceed memory budget", [] {
        TileCache c(8);
        auto t = std::make_shared<Tile>();
        t->pixels.resize(20);
        c.put({1, 0, 64, 0, 0, 0}, t);
        CHECK(c.bytes() == 0);
        CHECK(c.count() == 0);
    });
    auto dir =
        std::filesystem::temp_directory_path() /
        ("paperview-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    std::string key(64, 'a');
    test("annotations persist and replace atomically", [&] {
        AnnotationRepository repo(dir);
        AnnotationDocument d;
        d.add(stroke(2, {{10, 20}, {30, 40}, {55, 88}}));
        Snapshot s{d.pages(), {2, 3, .25, 1.8, false}};
        repo.save(key, s);
        auto loaded = repo.load(key);
        CHECK(loaded.pages.at(2).size() == 1);
        CHECK(loaded.pages.at(2)[0]->points[2].y == 88);
        CHECK(loaded.view.page == 2 && loaded.view.rotation == 3 && near(loaded.view.zoom, 1.8));
        d.add(stroke(0, {{5, 6}}));
        s.pages = d.pages();
        repo.save(key, s);
        CHECK(repo.load(key).pages.size() == 2);
        CHECK(!std::filesystem::exists(dir / (key + ".tmp")));
    });
    test("corrupt data is rejected without overwriting", [&] {
        auto p = dir / (key + ".ink");
        {
            std::ofstream out(p, std::ios::binary);
            out << "PAPER001";
        }
        bool rejected = false;
        try {
            AnnotationRepository(dir).load(key);
        } catch (...) {
            rejected = true;
        }
        CHECK(rejected);
        CHECK(std::filesystem::file_size(p) == 8);
    });
    test("repository rejects path traversal", [&] {
        bool rejected = false;
        try {
            AnnotationRepository(dir).save("../bad", {});
        } catch (...) {
            rejected = true;
        }
        CHECK(rejected);
    });
    std::filesystem::remove_all(dir);
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
