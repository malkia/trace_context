#include <chrono>
#include <memory>
#include <stdio.h>
#include <string_view>
#include <sys/syscall.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>

class TraceContext {

  struct Data {
    std::shared_ptr<Data> parent;
    std::shared_ptr<Data> previous_thread_local;
    std::string_view label;
    int level;
    pid_t threadID = syscall(SYS_gettid);
    std::chrono::steady_clock::time_point startTime;
    std::chrono::steady_clock::time_point endTime;
    Data() = delete;
    Data(const std::shared_ptr<Data> &parent_, std::string_view label_)
        : parent(parent_), label(label_),
          level(parent_ ? parent_->level + 1 : -1) {
      printf(" Data::Data() %.*s (pid=%d, level=%d)\n", int(label.size()),
             label.data(), threadID, level);
    }
    // TODO: Implement efficient thread-safe slab based allocator
    // Bounds:
    //    Maximum N threads, M levels, N*M
    ~Data() {
      printf("~Data::Data() %.*s (pid=%d, level=%d)\n", int(label.size()),
             label.data(), threadID, level);
    }
  };

  static inline thread_local std::shared_ptr<Data> current_thread_local =
      std::make_shared<Data>(nullptr, "ThreadLocal");

  struct Scope {
    std::shared_ptr<Data> data;

    Scope() = delete;

    Scope(const std::shared_ptr<Data> &parentData_, std::string_view label_)
        : data(std::make_shared<Data>(parentData_, label_)) {
      printf("    Scope::Scope tid=%d, level=%d, label=%.*s\n", data->threadID,
             data->level, int(data->label.size()), data->label.data());
      data->startTime = std::chrono::steady_clock::now();
      data->previous_thread_local = current_thread_local;
      current_thread_local = data;
    }

    ~Scope() {
      current_thread_local = data->previous_thread_local;
      data->endTime = std::chrono::steady_clock::now();
      const auto d = data.get();
      const auto p = d->parent.get();
      printf("    ~Scope::Scope tid=%d, level=%d, label=%.*s, %d, %d, parent active=%d\n",
             d->threadID, d->level, int(d->label.size()), d->label.data(),
             int(data.use_count()), int(d->parent.use_count()),
             p ? (p->endTime.time_since_epoch().count() ? 0 : 1) : -1);
    }
  };

  Scope scope;

public:
  TraceContext() : scope(current_thread_local, "noname1") {}
  TraceContext(std::string_view label) : scope(current_thread_local, label) {}
  TraceContext(const TraceContext &parent) : scope(parent.scope.data, {}) {}
  TraceContext(const TraceContext &parent, std::string_view label)
      : scope(parent.scope.data, label) {}
};

void test1() {
  TraceContext parentContext("parentContext");
  auto t = std::thread([&] {
    TraceContext childContext(parentContext, "childContext");
    auto t2 = std::thread(
        [&] { TraceContext childContext2(childContext, "childContext2"); });
    auto t3 = std::thread([&] {
      TraceContext detachedChildContext3(childContext, "detachedChildContext3");
      auto t4 = std::thread([&] {
        TraceContext childContext2(childContext, "detachedChildContext4");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
      });
      t4.detach();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });
    t3.detach();
    t2.join();
  });
  t.join();
}

int main(int argc, const char *argv[]) {
  TraceContext grandParentContext("grandParent");
  {
    test1();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    printf("ok1\n");
  }
  printf("ok2\n");
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  printf("ok3\n");
  return 0;
}
