#include <chronostore/database.hpp>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <implot.h>

#if defined(__APPLE__)
#define GL_SILENCE_DEPRECATION
#endif
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

constexpr std::size_t path_capacity = 1024U;
constexpr std::size_t measurement_capacity = 256U;
constexpr std::size_t tags_capacity = 1024U;
constexpr std::int64_t demo_start_timestamp = 1'700'000'000'000'000'000LL;
constexpr std::int64_t nanoseconds_per_second = 1'000'000'000LL;
constexpr std::size_t demo_sample_count = 240U;

template <std::size_t Size>
void set_text(std::array<char, Size>& destination, std::string_view source) {
    if (source.size() >= Size) {
        throw std::length_error("ChronoView text field exceeds its supported size");
    }

    destination.fill('\0');
    std::copy(source.begin(), source.end(), destination.begin());
}

std::string_view trim(std::string_view text) {
    const std::size_t first = text.find_first_not_of(" \t\r\n");

    if (first == std::string_view::npos) {
        return {};
    }

    const std::size_t last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1U);
}

std::vector<chronostore::Tag> parse_tags(std::string_view encoded) {
    std::vector<chronostore::Tag> tags;
    encoded = trim(encoded);

    if (encoded.empty()) {
        return tags;
    }

    std::size_t begin = 0U;

    while (begin <= encoded.size()) {
        const std::size_t comma = encoded.find(',', begin);
        const std::size_t end = comma == std::string_view::npos ? encoded.size() : comma;
        const std::string_view item = trim(encoded.substr(begin, end - begin));
        const std::size_t equals = item.find('=');

        if (item.empty() || equals == std::string_view::npos || equals == 0U) {
            throw std::invalid_argument("tags must use comma-separated key=value pairs");
        }

        const std::string_view key = trim(item.substr(0U, equals));
        const std::string_view value = trim(item.substr(equals + 1U));

        if (key.empty()) {
            throw std::invalid_argument("tag keys cannot be empty");
        }

        tags.emplace_back(std::string(key), std::string(value));

        if (comma == std::string_view::npos) {
            break;
        }

        begin = comma + 1U;
    }

    return tags;
}

std::string encode_tags(const std::vector<chronostore::Tag>& tags) {
    std::string encoded;

    for (std::size_t index = 0U; index < tags.size(); ++index) {
        if (index != 0U) {
            encoded += ',';
        }

        encoded += tags[index].key();
        encoded += '=';
        encoded += tags[index].value();
    }

    return encoded;
}

std::string series_label(const chronostore::SeriesKey& series) {
    std::string label = series.measurement();

    if (!series.tags().empty()) {
        label += "  [";
        label += encode_tags(series.tags());
        label += ']';
    }

    return label;
}

enum class QueryKind { none, point, latest, range };

class GuiState {
public:
    explicit GuiState(const std::filesystem::path& initial_path) {
        set_text(database_path_, initial_path.string());
        set_text(measurement_, "temperature");
        set_text(tags_, "room=lab");
    }

    template <typename Operation> void perform(std::string success_message, Operation&& operation) {
        try {
            std::forward<Operation>(operation)();
            status_ = std::move(success_message);
            status_is_error_ = false;
        } catch (const std::exception& error) {
            status_ = error.what();
            status_is_error_ = true;
        }
    }

    void open_database() {
        if (database_ != nullptr) {
            return;
        }

        if (flush_threshold_ > std::numeric_limits<std::size_t>::max()) {
            throw std::invalid_argument("flush threshold is too large");
        }

        chronostore::DatabaseOptions options;
        options.durability = sync_on_write_ ? chronostore::Durability::sync_on_write
                                            : chronostore::Durability::buffered;
        options.memtable_flush_threshold = static_cast<std::size_t>(flush_threshold_);

        auto opened = std::make_unique<chronostore::Database>(
            std::filesystem::path(database_path_.data()), options);
        database_ = std::move(opened);
        refresh_metadata();

        if (!known_series_.empty()) {
            select_series(0U);
        }
    }

    void close_database() {
        database_.reset();
        known_series_.clear();
        selected_series_ = -1;
        stats_ = {};
        clear_results();
        status_ = "No database open";
        status_is_error_ = false;
    }

    void refresh_metadata() {
        require_database();
        stats_ = database_->stats();
        known_series_ = database_->series();

        if (selected_series_ >= static_cast<int>(known_series_.size())) {
            selected_series_ = -1;
        }
    }

    void select_series(std::size_t index) {
        if (index >= known_series_.size()) {
            return;
        }

        selected_series_ = static_cast<int>(index);
        const chronostore::SeriesKey& series = known_series_[index];
        set_text(measurement_, series.measurement());
        set_text(tags_, encode_tags(series.tags()));
        clear_results();
    }

    void put_sample() {
        require_database();
        const chronostore::SeriesKey series = current_series();
        database_->put(series,
                       chronostore::Sample{chronostore::Timestamp{write_timestamp_}, write_value_});
        refresh_metadata();
        select_known_series(series);
    }

    void query_point() {
        require_database();
        single_result_ = database_->get(current_series(), chronostore::Timestamp{point_timestamp_});
        range_result_.clear();
        plot_x_.clear();
        plot_y_.clear();
        query_kind_ = QueryKind::point;
    }

    void query_latest() {
        require_database();
        single_result_ = database_->latest(current_series());
        range_result_.clear();
        plot_x_.clear();
        plot_y_.clear();
        query_kind_ = QueryKind::latest;
    }

    void query_range() {
        require_database();
        range_result_ = database_->range(current_series(), chronostore::Timestamp{range_start_},
                                         chronostore::Timestamp{range_end_});
        single_result_.reset();
        query_kind_ = QueryKind::range;
        rebuild_plot();
    }

    void generate_demo() {
        require_database();
        set_text(measurement_, "temperature");
        set_text(tags_, "room=lab");
        const chronostore::SeriesKey series = current_series();

        for (std::size_t index = 0U; index < demo_sample_count; ++index) {
            const auto offset = static_cast<std::int64_t>(index) * nanoseconds_per_second;
            const double wave = std::sin(static_cast<double>(index) * 0.12);
            const double drift = static_cast<double>(index) * 0.004;
            database_->put(
                series, chronostore::Sample{chronostore::Timestamp{demo_start_timestamp + offset},
                                            21.0 + wave * 2.2 + drift});
        }

        database_->flush();
        range_start_ = demo_start_timestamp;
        range_end_ = demo_start_timestamp +
                     static_cast<std::int64_t>(demo_sample_count) * nanoseconds_per_second;
        point_timestamp_ = demo_start_timestamp;
        write_timestamp_ = range_end_;
        refresh_metadata();
        select_known_series(series);
        query_range();
    }

    void sync() {
        require_database();
        database_->sync();
        refresh_metadata();
    }

    void flush() {
        require_database();
        database_->flush();
        refresh_metadata();
    }

    void compact() {
        require_database();
        database_->compact();
        refresh_metadata();
    }

    [[nodiscard]] bool connected() const noexcept {
        return database_ != nullptr;
    }

    [[nodiscard]] std::array<char, path_capacity>& database_path() noexcept {
        return database_path_;
    }

    [[nodiscard]] std::array<char, measurement_capacity>& measurement() noexcept {
        return measurement_;
    }

    [[nodiscard]] std::array<char, tags_capacity>& tags() noexcept {
        return tags_;
    }

    [[nodiscard]] bool& sync_on_write() noexcept {
        return sync_on_write_;
    }

    [[nodiscard]] std::uint64_t& flush_threshold() noexcept {
        return flush_threshold_;
    }

    [[nodiscard]] std::int64_t& point_timestamp() noexcept {
        return point_timestamp_;
    }

    [[nodiscard]] std::int64_t& range_start() noexcept {
        return range_start_;
    }

    [[nodiscard]] std::int64_t& range_end() noexcept {
        return range_end_;
    }

    [[nodiscard]] std::int64_t& write_timestamp() noexcept {
        return write_timestamp_;
    }

    [[nodiscard]] double& write_value() noexcept {
        return write_value_;
    }

    [[nodiscard]] const chronostore::DatabaseStats& stats() const noexcept {
        return stats_;
    }

    [[nodiscard]] const std::vector<chronostore::SeriesKey>& known_series() const noexcept {
        return known_series_;
    }

    [[nodiscard]] int selected_series() const noexcept {
        return selected_series_;
    }

    [[nodiscard]] QueryKind query_kind() const noexcept {
        return query_kind_;
    }

    [[nodiscard]] const std::optional<chronostore::Sample>& single_result() const noexcept {
        return single_result_;
    }

    [[nodiscard]] const std::vector<chronostore::Sample>& range_result() const noexcept {
        return range_result_;
    }

    [[nodiscard]] const std::vector<double>& plot_x() const noexcept {
        return plot_x_;
    }

    [[nodiscard]] const std::vector<double>& plot_y() const noexcept {
        return plot_y_;
    }

    [[nodiscard]] const std::string& status() const noexcept {
        return status_;
    }

    [[nodiscard]] bool status_is_error() const noexcept {
        return status_is_error_;
    }

private:
    [[nodiscard]] chronostore::SeriesKey current_series() const {
        return chronostore::SeriesKey{measurement_.data(), parse_tags(tags_.data())};
    }

    void require_database() const {
        if (database_ == nullptr) {
            throw std::logic_error("no database is open");
        }
    }

    void rebuild_plot() {
        plot_x_.clear();
        plot_y_.clear();

        if (range_result_.empty()) {
            return;
        }

        plot_x_.reserve(range_result_.size());
        plot_y_.reserve(range_result_.size());
        const std::int64_t origin = range_result_.front().timestamp().nanoseconds_since_epoch();

        for (const chronostore::Sample& sample : range_result_) {
            const long double offset =
                static_cast<long double>(sample.timestamp().nanoseconds_since_epoch()) -
                static_cast<long double>(origin);
            plot_x_.push_back(static_cast<double>(offset));
            plot_y_.push_back(sample.value());
        }
    }

    void select_known_series(const chronostore::SeriesKey& series) {
        for (std::size_t index = 0U; index < known_series_.size(); ++index) {
            if (known_series_[index] == series) {
                selected_series_ = static_cast<int>(index);
                return;
            }
        }
    }

    void clear_results() {
        query_kind_ = QueryKind::none;
        single_result_.reset();
        range_result_.clear();
        plot_x_.clear();
        plot_y_.clear();
    }

    std::array<char, path_capacity> database_path_{};
    std::array<char, measurement_capacity> measurement_{};
    std::array<char, tags_capacity> tags_{};
    bool sync_on_write_{true};
    std::uint64_t flush_threshold_{4096U};
    std::int64_t point_timestamp_{demo_start_timestamp};
    std::int64_t range_start_{demo_start_timestamp};
    std::int64_t range_end_{demo_start_timestamp +
                            static_cast<std::int64_t>(demo_sample_count) * nanoseconds_per_second};
    std::int64_t write_timestamp_{demo_start_timestamp};
    double write_value_{21.5};
    std::unique_ptr<chronostore::Database> database_;
    chronostore::DatabaseStats stats_{};
    std::vector<chronostore::SeriesKey> known_series_;
    int selected_series_{-1};
    QueryKind query_kind_{QueryKind::none};
    std::optional<chronostore::Sample> single_result_;
    std::vector<chronostore::Sample> range_result_;
    std::vector<double> plot_x_;
    std::vector<double> plot_y_;
    std::string status_{"No database open"};
    bool status_is_error_{false};
};

void draw_status(const GuiState& state) {
    const ImVec4 color = state.status_is_error() ? ImVec4{0.95F, 0.42F, 0.35F, 1.0F}
                                                 : ImVec4{0.35F, 0.82F, 0.58F, 1.0F};
    ImGui::TextColored(color, "%s", state.status().c_str());
}

void draw_stats(const chronostore::DatabaseStats& stats) {
    if (ImGui::BeginTable("stats", 2, ImGuiTableFlags_SizingStretchProp)) {
        const auto row = [](const char* label, std::uint64_t value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%llu", static_cast<unsigned long long>(value));
        };

        row("Samples", stats.sample_count);
        row("In memory", stats.memory_sample_count);
        row("Segments", stats.segment_count);
        row("WAL bytes", stats.wal_size_bytes);
        ImGui::EndTable();
    }
}

void draw_series_editor(GuiState& state) {
    ImGui::TextUnformatted("Measurement");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputText("##measurement", state.measurement().data(), state.measurement().size());
    ImGui::TextUnformatted("Tags");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputText("##tags", state.tags().data(), state.tags().size());
}

void draw_sidebar(GuiState& state) {
    ImGui::SeparatorText("Database");
    ImGui::BeginDisabled(state.connected());
    ImGui::TextUnformatted("Path");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputText("##database_path", state.database_path().data(), state.database_path().size());
    ImGui::Checkbox("Sync on write", &state.sync_on_write());
    ImGui::TextUnformatted("Flush threshold");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputScalar("##flush_threshold", ImGuiDataType_U64, &state.flush_threshold());
    ImGui::EndDisabled();

    if (!state.connected()) {
        if (ImGui::Button("Open", ImVec2{-1.0F, 0.0F})) {
            state.perform("Database opened", [&] { state.open_database(); });
        }
    } else {
        if (ImGui::Button("Refresh", ImVec2{112.0F, 0.0F})) {
            state.perform("Metadata refreshed", [&] { state.refresh_metadata(); });
        }

        ImGui::SameLine();

        if (ImGui::Button("Close", ImVec2{-1.0F, 0.0F})) {
            state.close_database();
        }
    }

    ImGui::SeparatorText("Status");
    draw_status(state);

    ImGui::SeparatorText("Engine");
    draw_stats(state.stats());

    ImGui::SeparatorText("Series");
    const float list_height = std::max(140.0F, ImGui::GetContentRegionAvail().y);

    if (ImGui::BeginListBox("##series", ImVec2{-1.0F, list_height})) {
        for (std::size_t index = 0U; index < state.known_series().size(); ++index) {
            const std::string label = series_label(state.known_series()[index]);
            const bool selected = state.selected_series() == static_cast<int>(index);

            if (ImGui::Selectable(label.c_str(), selected)) {
                state.select_series(index);
            }

            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", label.c_str());
            }
        }

        if (state.known_series().empty()) {
            ImGui::TextDisabled("No series");
        }

        ImGui::EndListBox();
    }
}

void draw_sample_table(const std::vector<chronostore::Sample>& samples, const char* identifier) {
    if (!ImGui::BeginTable(identifier, 2,
                           ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                           ImVec2{0.0F, 240.0F})) {
        return;
    }

    ImGui::TableSetupColumn("Timestamp (ns)", ImGuiTableColumnFlags_WidthStretch, 2.0F);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0F);
    ImGui::TableHeadersRow();

    const std::size_t visible_count =
        std::min(samples.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()));
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(visible_count));

    while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
            const chronostore::Sample& sample = samples[static_cast<std::size_t>(row)];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%lld",
                        static_cast<long long>(sample.timestamp().nanoseconds_since_epoch()));
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.17g", sample.value());
        }
    }

    ImGui::EndTable();
}

void draw_results(const GuiState& state) {
    ImGui::SeparatorText("Results");

    if (state.query_kind() == QueryKind::none) {
        ImGui::TextDisabled("No query run");
        return;
    }

    if (state.query_kind() == QueryKind::point || state.query_kind() == QueryKind::latest) {
        if (!state.single_result().has_value()) {
            ImGui::TextDisabled("No sample found");
            return;
        }

        draw_sample_table({state.single_result().value()}, "single_result");
        return;
    }

    ImGui::Text("%llu samples", static_cast<unsigned long long>(state.range_result().size()));

    if (state.range_result().empty()) {
        ImGui::TextDisabled("No samples in range");
        return;
    }

    if (ImPlot::BeginPlot("##range_plot", ImVec2{-1.0F, 280.0F})) {
        ImPlot::SetupAxes("Nanoseconds from first sample", "Value", ImPlotAxisFlags_AutoFit,
                          ImPlotAxisFlags_AutoFit);
        const std::size_t plot_count = std::min(
            state.plot_x().size(), static_cast<std::size_t>(std::numeric_limits<int>::max()));
        ImPlotSpec plot_spec;
        plot_spec.LineColor = ImVec4{0.96F, 0.45F, 0.34F, 1.0F};
        plot_spec.LineWeight = 2.0F;
        ImPlot::PlotLine("Value", state.plot_x().data(), state.plot_y().data(),
                         static_cast<int>(plot_count), plot_spec);
        ImPlot::EndPlot();
    }

    draw_sample_table(state.range_result(), "range_result");
}

void draw_explore_tab(GuiState& state) {
    draw_series_editor(state);
    ImGui::SeparatorText("Query");

    if (ImGui::Button("Latest", ImVec2{112.0F, 0.0F})) {
        state.perform("Latest query complete", [&] { state.query_latest(); });
    }

    ImGui::TextUnformatted("Point timestamp");

    if (ImGui::BeginTable("point_input", 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("input", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("action", ImGuiTableColumnFlags_WidthFixed, 112.0F);
        ImGui::TableNextColumn();
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputScalar("##point_timestamp", ImGuiDataType_S64, &state.point_timestamp());
        ImGui::TableNextColumn();

        if (ImGui::Button("Get", ImVec2{-1.0F, 0.0F})) {
            state.perform("Point query complete", [&] { state.query_point(); });
        }

        ImGui::EndTable();
    }

    if (ImGui::BeginTable("range_inputs", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Range start");
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputScalar("##range_start", ImGuiDataType_S64, &state.range_start());
        ImGui::TableNextColumn();
        ImGui::TextUnformatted("Range end");
        ImGui::SetNextItemWidth(-1.0F);
        ImGui::InputScalar("##range_end", ImGuiDataType_S64, &state.range_end());
        ImGui::EndTable();
    }

    if (ImGui::Button("Run range", ImVec2{112.0F, 0.0F})) {
        state.perform("Range query complete", [&] { state.query_range(); });
    }

    draw_results(state);
}

void draw_write_tab(GuiState& state) {
    draw_series_editor(state);
    ImGui::SeparatorText("Sample");
    ImGui::TextUnformatted("Timestamp");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputScalar("##write_timestamp", ImGuiDataType_S64, &state.write_timestamp());
    ImGui::TextUnformatted("Value");
    ImGui::SetNextItemWidth(-1.0F);
    ImGui::InputDouble("##write_value", &state.write_value(), 0.0, 0.0, "%.17g");

    if (ImGui::Button("Put", ImVec2{112.0F, 0.0F})) {
        state.perform("Sample written", [&] { state.put_sample(); });
    }

    ImGui::SameLine();

    if (ImGui::Button("Generate demo", ImVec2{144.0F, 0.0F})) {
        state.perform("Demo series generated", [&] { state.generate_demo(); });
    }
}

void draw_maintenance_tab(GuiState& state) {
    ImGui::SeparatorText("Engine");
    draw_stats(state.stats());
    ImGui::SeparatorText("Maintenance");

    if (ImGui::Button("Sync WAL", ImVec2{128.0F, 0.0F})) {
        state.perform("WAL synchronized", [&] { state.sync(); });
    }

    ImGui::SameLine();

    if (ImGui::Button("Flush", ImVec2{128.0F, 0.0F})) {
        state.perform("MemTable flushed", [&] { state.flush(); });
    }

    ImGui::SameLine();

    if (ImGui::Button("Compact", ImVec2{128.0F, 0.0F})) {
        state.perform("Segments compacted", [&] { state.compact(); });
    }
}

void draw_main_panel(GuiState& state) {
    ImGui::BeginDisabled(!state.connected());

    if (ImGui::BeginTabBar("views")) {
        if (ImGui::BeginTabItem("Explore")) {
            draw_explore_tab(state);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Write")) {
            draw_write_tab(state);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Maintenance")) {
            draw_maintenance_tab(state);
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::EndDisabled();
}

void draw_application(GuiState& state) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    constexpr ImGuiWindowFlags root_flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::Begin("ChronoViewRoot", nullptr, root_flags);
    ImGui::TextColored(ImVec4{0.25F, 0.82F, 0.78F, 1.0F}, "ChronoView");
    ImGui::SameLine();
    ImGui::TextDisabled("ChronoStore 0.1");
    ImGui::Separator();

    const float sidebar_width = std::min(340.0F, ImGui::GetContentRegionAvail().x * 0.32F);

    if (ImGui::BeginChild("sidebar", ImVec2{sidebar_width, 0.0F}, ImGuiChildFlags_Borders)) {
        draw_sidebar(state);
    }
    ImGui::EndChild();
    ImGui::SameLine();

    if (ImGui::BeginChild("main", ImVec2{0.0F, 0.0F}, ImGuiChildFlags_Borders,
                          ImGuiWindowFlags_AlwaysVerticalScrollbar)) {
        draw_main_panel(state);
    }
    ImGui::EndChild();
    ImGui::End();
}

void apply_style() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0F;
    style.ChildRounding = 4.0F;
    style.FrameRounding = 3.0F;
    style.PopupRounding = 4.0F;
    style.ScrollbarRounding = 3.0F;
    style.GrabRounding = 3.0F;
    style.TabRounding = 3.0F;
    style.WindowPadding = ImVec2{12.0F, 12.0F};
    style.FramePadding = ImVec2{9.0F, 6.0F};
    style.ItemSpacing = ImVec2{9.0F, 8.0F};
    style.Colors[ImGuiCol_WindowBg] = ImVec4{0.075F, 0.082F, 0.095F, 1.0F};
    style.Colors[ImGuiCol_ChildBg] = ImVec4{0.105F, 0.115F, 0.13F, 1.0F};
    style.Colors[ImGuiCol_Border] = ImVec4{0.24F, 0.26F, 0.29F, 1.0F};
    style.Colors[ImGuiCol_FrameBg] = ImVec4{0.15F, 0.16F, 0.18F, 1.0F};
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4{0.19F, 0.22F, 0.23F, 1.0F};
    style.Colors[ImGuiCol_Button] = ImVec4{0.08F, 0.43F, 0.44F, 1.0F};
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4{0.11F, 0.56F, 0.55F, 1.0F};
    style.Colors[ImGuiCol_ButtonActive] = ImVec4{0.07F, 0.35F, 0.36F, 1.0F};
    style.Colors[ImGuiCol_Header] = ImVec4{0.22F, 0.28F, 0.3F, 1.0F};
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4{0.12F, 0.46F, 0.46F, 1.0F};
    style.Colors[ImGuiCol_Tab] = ImVec4{0.16F, 0.18F, 0.2F, 1.0F};
    style.Colors[ImGuiCol_TabHovered] = ImVec4{0.11F, 0.51F, 0.5F, 1.0F};
    style.Colors[ImGuiCol_TabSelected] = ImVec4{0.08F, 0.43F, 0.44F, 1.0F};
    style.Colors[ImGuiCol_TabSelectedOverline] = ImVec4{0.25F, 0.82F, 0.78F, 1.0F};
    style.Colors[ImGuiCol_TabDimmed] = ImVec4{0.13F, 0.14F, 0.16F, 1.0F};
    style.Colors[ImGuiCol_TabDimmedSelected] = ImVec4{0.07F, 0.35F, 0.36F, 1.0F};
    style.Colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4{0.25F, 0.82F, 0.78F, 1.0F};
    style.Colors[ImGuiCol_CheckMark] = ImVec4{0.96F, 0.68F, 0.3F, 1.0F};

    ImPlotStyle& plot_style = ImPlot::GetStyle();
    plot_style.Colors[ImPlotCol_PlotBg] = ImVec4{0.08F, 0.085F, 0.095F, 1.0F};
    plot_style.Colors[ImPlotCol_PlotBorder] = ImVec4{0.3F, 0.32F, 0.35F, 1.0F};
}

void glfw_error_callback(int error, const char* description) {
    std::cerr << "GLFW error " << error << ": " << description << '\n';
}

int run_gui(const std::filesystem::path& path, bool open_on_start, bool generate_demo) {
    glfwSetErrorCallback(glfw_error_callback);

    if (glfwInit() == GLFW_FALSE) {
        throw std::runtime_error("failed to initialize GLFW");
    }

#if defined(__APPLE__)
    const char* glsl_version = "#version 150";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
    const char* glsl_version = "#version 130";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

    GLFWwindow* window = glfwCreateWindow(1280, 800, "ChronoView", nullptr, nullptr);

    if (window == nullptr) {
        glfwTerminate();
        throw std::runtime_error("failed to create ChronoView window");
    }

    glfwSetWindowSizeLimits(window, 960, 640, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    apply_style();

    if (!ImGui_ImplGlfw_InitForOpenGL(window, true) || !ImGui_ImplOpenGL3_Init(glsl_version)) {
        ImPlot::DestroyContext();
        ImGui::DestroyContext();
        glfwDestroyWindow(window);
        glfwTerminate();
        throw std::runtime_error("failed to initialize the ChronoView renderer");
    }

    GuiState state(path);

    if (open_on_start) {
        state.perform("Database opened", [&] { state.open_database(); });
    }

    if (generate_demo && state.connected()) {
        state.perform("Demo series generated", [&] { state.generate_demo(); });
    }

    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        glfwPollEvents();
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        draw_application(state);
        ImGui::Render();

        int display_width = 0;
        int display_height = 0;
        glfwGetFramebufferSize(window, &display_width, &display_height);
        glViewport(0, 0, display_width, display_height);
        glClearColor(0.075F, 0.082F, 0.095F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    state.close_database();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}

} // namespace

int main(int argument_count, char* arguments[]) {
    try {
        std::filesystem::path database_path{"chronostore-gui-db"};
        bool path_was_provided = false;
        bool generate_demo = false;

        for (int index = 1; index < argument_count; ++index) {
            const std::string_view argument(arguments[index]);

            if (argument == "--demo") {
                generate_demo = true;
                continue;
            }

            if (path_was_provided) {
                throw std::invalid_argument("usage: chronoview [database-path] [--demo]");
            }

            database_path = std::filesystem::path(argument);
            path_was_provided = true;
        }

        const bool open_on_start = path_was_provided || generate_demo;
        return run_gui(database_path, open_on_start, generate_demo);
    } catch (const std::exception& error) {
        std::cerr << "chronoview: " << error.what() << '\n';
        return 1;
    }
}
