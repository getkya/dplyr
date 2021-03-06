#include <dplyr/main.h>

#include <boost/scoped_ptr.hpp>

#include <tools/Quosure.h>

#include <dplyr/GroupedDataFrame.h>

#include <dplyr/Result/LazyRowwiseSubsets.h>
#include <dplyr/Result/GroupedCallReducer.h>
#include <dplyr/Result/CallProxy.h>

#include <dplyr/NamedListAccumulator.h>
#include <dplyr/Groups.h>

using namespace Rcpp;
using namespace dplyr;

template <typename Data, typename Subsets>
SEXP summarise_grouped(const DataFrame& df, const QuosureList& dots) {
  Data gdf(df);

  int nexpr = dots.size();
  int nvars = gdf.nvars();
  check_not_groups(dots, gdf);

  LOG_VERBOSE << "copying " << nvars << " variables to accumulator";

  NamedListAccumulator<Data> accumulator;
  int i=0;
  List results(nvars + nexpr);
  for (; i<nvars; i++) {
    LOG_VERBOSE << "copying " << gdf.symbol(i).get_cstring();
    results[i] = shared_SEXP(gdf.label(i));
    accumulator.set(gdf.symbol(i), results[i]);
  }

  LOG_VERBOSE <<  "processing " << nexpr << " variables";

  Subsets subsets(gdf);
  for (int k=0; k<nexpr; k++, i++) {
    LOG_VERBOSE << "processing variable " << k;
    Rcpp::checkUserInterrupt();
    const NamedQuosure& quosure = dots[k];
    const Environment& env = quosure.env();

    LOG_VERBOSE << "processing variable " << quosure.name().get_cstring();

    Shield<SEXP> expr_(quosure.expr());
    SEXP expr = expr_;
    boost::scoped_ptr<Result> res(get_handler(expr, subsets, env));

    // If we could not find a direct Result,
    // we can use a GroupedCallReducer which will callback to R.
    // Note that the GroupedCallReducer currently doesn't apply
    // special treatment to summary variables, for which hybrid
    // evaluation should be turned off completely (#2312)
    if (!res) {
      res.reset(new GroupedCallReducer<Data, Subsets>(quosure.expr(), subsets, env));
    }
    RObject result = res->process(gdf);
    results[i] = result;
    accumulator.set(quosure.name(), result);
    subsets.input_summarised(quosure.name(), SummarisedVariable(result));

  }

  List out = accumulator;
  copy_most_attributes(out, df);
  out.names() = accumulator.names();

  int nr = gdf.ngroups();
  set_rownames(out, nr);

  if (gdf.nvars() > 1) {
    set_class(out, classes_grouped<Data>());
    SymbolVector vars = get_vars(gdf.data());
    vars.remove(gdf.nvars() - 1);
    set_vars(out, vars);
    out.attr("labels") = R_NilValue;
    out.attr("indices") = R_NilValue;
    out.attr("group_sizes") = R_NilValue;
    out.attr("biggest_group_size") = R_NilValue;

    out.attr("drop") = true;
  } else {
    set_class(out, classes_not_grouped());
    SET_ATTRIB(out, strip_group_attributes(out));
  }

  return out;
}


SEXP summarise_not_grouped(DataFrame df, const QuosureList& dots) {
  int nexpr = dots.size();
  if (nexpr == 0) return DataFrame();

  LazySubsets subsets(df);
  NamedListAccumulator<DataFrame> accumulator;
  List results(nexpr);

  for (int i=0; i<nexpr; i++) {
    Rcpp::checkUserInterrupt();

    const NamedQuosure& quosure = dots[i];
    Environment env = quosure.env();
    Shield<SEXP> expr_(quosure.expr());
    SEXP expr = expr_;
    boost::scoped_ptr<Result> res(get_handler(expr, subsets, env));
    SEXP result;
    if (res) {
      result = results[i] = res->process(FullDataFrame(df));
    } else {
      result = results[i] = CallProxy(quosure.expr(), subsets, env).eval();
    }
    check_length(Rf_length(result), 1, "a summary value");
    accumulator.set(quosure.name(), result);
    subsets.input(quosure.name(), result);
  }
  List data = accumulator;
  copy_most_attributes(data, df);
  data.names() = accumulator.names();
  set_rownames(data, 1);
  return data;
}

// [[Rcpp::export]]
SEXP summarise_impl(DataFrame df, QuosureList dots) {
  if (df.size() == 0) return df;
  check_valid_colnames(df);
  if (is<RowwiseDataFrame>(df)) {
    return summarise_grouped<RowwiseDataFrame, LazyRowwiseSubsets>(df, dots);
  } else if (is<GroupedDataFrame>(df)) {
    return summarise_grouped<GroupedDataFrame, LazyGroupedSubsets>(df, dots);
  } else {
    return summarise_not_grouped(df, dots);
  }
}
