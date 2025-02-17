#pragma once

// stuffs for hot loading
//
// To make a resource hot-reloadable, it needs to know somehow if
// it has changed.

struct hotmark_t {
    void *ptr;
    void *load_params;
    bool modified = false;
    std::vector<hotmark_t *> dependonme;
};

class hotload_t {
public:
    template <typename T, typename LT>
    void add_file_watch(T *object, hotmark_t &mark, const char *path, std::function<void(T &, LT &)>&& on_modified) {
        mark.ptr = object;
        mark.load_params = object;
        mark.modified = false;

        auto on_modified_cb = [object, on_modified = std::move(on_modified)](std::string path, void *userdata) {
            on_modified(*object, *static_cast<LT *>(userdata));

            for (auto &o : object->dependencies) {
                o->modified = true;
            }
        };

        watcher.add_watch(path, on_modified_cb, &mark);
    }

    void add_dependency(hotmark_t &mark, hotmark_t &dependency) {
        mark.dependencies.push_back(&dependency);
    }

    void poll_updates() {
        watcher.process_watches();
    }
private:
    fswatcher_t watcher;
};
