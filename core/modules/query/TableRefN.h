// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2012-2013 LSST Corporation.
 *
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the LSST License Statement and
 * the GNU General Public License along with this program.  If not,
 * see <http://www.lsstcorp.org/LegalNotices/>.
 */
#ifndef LSST_QSERV_MASTER_TABLEREFN_H
#define LSST_QSERV_MASTER_TABLEREFN_H
/**
  * @file TableRefN.h
  *
  * @brief Declarations for TableRefN and subclasses SimpleTableN and JoinRefN
  *
  * @author Daniel L. Wang, SLAC
  */
#include <stdexcept>
#include <string>
#include <list>
#include <iostream>
#include <boost/shared_ptr.hpp>
#include "query/QueryTemplate.h"

namespace lsst {
namespace qserv {
namespace master {
class QueryTemplate; // Forward
class JoinSpec;

/// TableRefN is a parsed table reference node
class TableRefN {
public:
    typedef boost::shared_ptr<TableRefN> Ptr;
    typedef std::list<Ptr> PtrList;
    virtual ~TableRefN() {}

    virtual bool isSimple() const = 0;

    virtual std::ostream& putStream(std::ostream& os) const = 0;
    virtual void putTemplate(QueryTemplate& qt) const = 0;
    // Modifiers:
    virtual void setDb(std::string const& db) = 0;
    virtual void setTable(std::string const& table) = 0;

    class Func {
    public:
        virtual ~Func() {}
        virtual void operator()(TableRefN& t) {}
    };
    class FuncConst {
    public:
        virtual ~FuncConst() {}
        virtual void operator()(TableRefN const& t) {}
    };
    template <class F>
    class Fwrapper {
    public:
        Fwrapper(F& f_) : f(f_) {}
        inline void operator()(TableRefN::Ptr& t) {
            if(t.get()) { t->apply(f); } }
        inline void operator()(TableRefN::Ptr const& t) {
            if(t.get()) { t->apply(f); } }
        F& f;
    };

    // apply f() over all all tableRefns in depth-first order (for compound
    // tablerefs). Always applies non-const version.
    virtual void apply(Func& f) {}
    virtual void apply(FuncConst& f) {}

    struct Pfunc {
        virtual std::list<TableRefN::Ptr> operator()(SimpleTableN const& t) = 0;
    };
    virtual std::list<TableRefN::Ptr> permute(Pfunc& p) const;
    class render;
protected:
};
std::ostream& operator<<(std::ostream& os, TableRefN const& refN);
std::ostream& operator<<(std::ostream& os, TableRefN const* refN);

/// class render: helper functor for QueryTemplate conversion
class TableRefN::render {
public:
    render(QueryTemplate& qt) : _qt(qt), _count(0) {}
    void operator()(TableRefN const& trn);
    void operator()(TableRefN::Ptr const trn) {
        if(trn.get()) (*this)(*trn);
    }
    QueryTemplate& _qt;
    int _count;
};

/// SimpleTableN is the simplest TableRefN: a db.table reference
class SimpleTableN : public TableRefN {
public:
    typedef boost::shared_ptr<SimpleTableN> Ptr;
    SimpleTableN(std::string const& db_, std::string const& table_,
                 std::string const& alias_)
        : alias(alias_), db(db_), table(table_)  {
        if(table_.empty()) { throw std::logic_error("SimpleTableN without table"); }
    }

    virtual bool isSimple() const { return true; }
    virtual std::string const& getDb() const { return db; }
    virtual std::string const& getTable() const { return table; }
    virtual std::string const& getAlias() const { return alias; }

    virtual std::ostream& putStream(std::ostream& os) const {
        os << "Table(" << db << "." << table << ")";
        if(!alias.empty()) { os << " AS " << alias; }
        return os;
    }
    virtual void putTemplate(QueryTemplate& qt) const {
        qt.append(*this);
        if(!alias.empty()) {
            qt.append("AS");
            qt.append(alias);
        }
    }
    // Modifiers
    virtual void setAlias(std::string const& a) { alias=a; }
    virtual void setDb(std::string const& db_) { db = db_; }
    virtual void setTable(std::string const& table_) { table = table_; }
    virtual void apply(Func& f) { f(*this); }
    virtual void apply(FuncConst& f) const { f(*this); }
    virtual std::list<TableRefN::Ptr> permute(Pfunc& p) const;
    virtual SimpleTableN::Ptr clone() const {
        return Ptr(new SimpleTableN(*this));
    }

protected:
    std::string alias;
    std::string db;
    std::string table;
};

/// JoinRefN is a more complex TableRefN: the JOIN of two TableRefN. It is
/// flattened to only allow db.table as its joining tables (no additional
/// nesting is allowed).
/// Implementation is incomplete/broken.
class JoinRefN : public TableRefN {
public:
    typedef boost::shared_ptr<JoinRefN> Ptr;
    enum JoinType {DEFAULT, INNER, LEFT, RIGHT, FULL, CROSS, UNION};

    JoinRefN(TableRefN::Ptr left_, TableRefN::Ptr right_,
             JoinType jt, bool isNatural_,
             boost::shared_ptr<JoinSpec> spec_)
        : left(left_), right(right_),
          joinType(jt),
          isNatural(isNatural_),
          spec(spec_) {}

    virtual bool isSimple() const { return false; }
    bool getIsNatural() const { return isNatural; }
    JoinType getJoinType() const { return joinType; }
    TableRefN const* getLeftRO() const { return left.get(); }
    TableRefN const* getRightRO() const { return right.get(); }
    TableRefN::Ptr getLeft() { return left; }
    TableRefN::Ptr getRight() { return right; }

    JoinSpec const* getSpecRO() const { return spec.get(); }
    boost::shared_ptr<JoinSpec> getSpec() { return spec; }

    virtual std::ostream& putStream(std::ostream& os) const;
    virtual void putTemplate(QueryTemplate& qt) const;
    // Modifiers
    void setLeft(TableRefN::Ptr t) { left = t; }
    void setRight(TableRefN::Ptr t) { right = t; }
    void setJoinType(JoinType jt, bool isNatural_) {
        joinType = jt; isNatural = isNatural_; }
    virtual void setDb(std::string const&) {} // FIXME: ignore?
    virtual void setTable(std::string const&) {} // FIXME: ignore?
    virtual void apply(Func& f);
    virtual void apply(FuncConst& f) const;
    virtual std::list<TableRefN::Ptr> permute(Pfunc& p) const;

protected:
    void _putJoinTemplate(QueryTemplate& qt) const;

    TableRefN::Ptr left;
    TableRefN::Ptr right;
    JoinType joinType;
    bool isNatural;
    boost::shared_ptr<JoinSpec> spec;
};


class JoinRef;
typedef std::list<boost::shared_ptr<JoinRef> > JoinRefList;
/// TableRefN2 is a parsed table reference node
// table_ref :
//   table_ref_aux (options{greedy=true;}:qualified_join | cross_join)*
// table_ref_aux :
//   (n:table_name | /*derived_table*/q:table_subquery) ((as:"as")? c:correlation_name (LEFT_PAREN derived_column_list RIGHT_PAREN)?)?
class TableRefN2 {
public:
    typedef boost::shared_ptr<TableRefN2> Ptr;
    typedef std::list<Ptr> PtrList;
    virtual ~TableRefN2() {}

    virtual std::ostream& putStream(std::ostream& os) const;
    virtual void putTemplate(QueryTemplate& qt) const;

    bool isSimple() const { return joinRefList.empty(); }
    virtual std::string const& getDb() const { return db; }
    virtual std::string const& getTable() const { return table; }
    virtual std::string const& getAlias() const { return alias; }

    virtual JoinRef const* getJoinRef(int i=0) const;
    virtual JoinRefList const& getJoins() const {
        return joinRefList; }

    // Modifiers
    virtual void setAlias(std::string const& a) { alias=a; }
    virtual void setDb(std::string const& db_) { db = db_; }
    virtual void setTable(std::string const& table_) { table = table_; }
    virtual JoinRefList& getJoins() {
        return joinRefList; }

    struct Pfunc {
        virtual std::list<TableRefN::Ptr> operator()(SimpleTableN const& t) = 0;
    };
    virtual std::list<TableRefN::Ptr> permute(Pfunc& p) const;
    virtual TableRefN2::Ptr clone() const {
        // FIXME do deep copy.
        return Ptr(new TableRefN2(*this));
    }

    class render;
private:
    std::string alias;
    std::string db;
    std::string table;
    JoinRefList joinRefList;
    
};

// Containers
typedef std::list<TableRefN::Ptr> TableRefnList;
typedef boost::shared_ptr<TableRefnList> TableRefnListPtr;

}}} // namespace lsst::qserv::master


#endif // LSST_QSERV_MASTER_TABLEREFN_H
