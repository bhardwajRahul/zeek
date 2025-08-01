// See the file "COPYING" in the main distribution directory for copyright.

#include "zeek/telemetry/Histogram.h"

#include <algorithm>
#include <span>

using namespace zeek::telemetry;

double Histogram::Sum() const noexcept {
    auto metric = handle.Collect();
    return static_cast<double>(metric.histogram.sample_sum);
}

Histogram::Histogram(FamilyType* family, const prometheus::Labels& labels,
                     prometheus::Histogram::BucketBoundaries bounds) noexcept
    : handle(family->Add(labels, std::move(bounds))), labels(labels) {}

std::shared_ptr<Histogram> HistogramFamily::GetOrAdd(std::span<const LabelView> labels) {
    prometheus::Labels p_labels = detail::BuildPrometheusLabels(labels);

    auto check = [&](const std::shared_ptr<Histogram>& histo) { return histo->CompareLabels(p_labels); };

    if ( auto it = std::ranges::find_if(histograms, check); it != histograms.end() )
        return *it;

    auto histogram = std::make_shared<Histogram>(family, p_labels, boundaries);
    histograms.push_back(histogram);
    return histogram;
}

/**
 * @copydoc GetOrAdd
 */
std::shared_ptr<Histogram> HistogramFamily::GetOrAdd(std::initializer_list<LabelView> labels) {
    return GetOrAdd(std::span{labels.begin(), labels.size()});
}

HistogramFamily::HistogramFamily(prometheus::Family<prometheus::Histogram>* family, std::span<const double> bounds,
                                 std::span<const std::string_view> labels)
    : MetricFamily(labels), family(family) {
    std::ranges::copy(bounds, std::back_inserter(boundaries));
}
