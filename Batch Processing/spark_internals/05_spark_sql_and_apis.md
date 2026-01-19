# Spark SQL & APIs Deep Dive: DataFrame, Dataset, and SQL

**A comprehensive guide to Spark's high-level APIs, Catalyst optimizer, and Tungsten execution engine**

---

## Part 1: The Evolution - From RDD to DataFrame

### 1.1 The RDD Problem: Why We Needed Something Better

When Spark was first released, the **RDD (Resilient Distributed Dataset)** was the only API available. While powerful and flexible, RDDs had significant limitations that became apparent as Spark usage grew.

#### The Pain Points with RDDs

**Problem 1: No Schema - Just Opaque Data**

```scala
// Reading CSV with RDD - manual parsing
val userData = sc.textFile("users.csv")
  .map(line => line.split(","))
  .filter(fields => fields.length == 4)  // Hope there are 4 fields!
  .filter(fields => fields(2).toInt > 18)  // Hope field 2 is age!
  .map(fields => (fields(0), fields(1), fields(2).toInt, fields(3)))

// Problems:
// ❌ No schema validation
// ❌ Runtime errors if data format changes
// ❌ Hard to understand what fields mean
// ❌ Manual type conversion
```

**Problem 2: No Optimization - Opaque Lambdas**

```scala
val result = userData
  .filter(user => user._3 > 18 && user._3 < 65)
  .map(user => (user._4, 1))
  .reduceByKey(_ + _)

// Spark sees:
// - filter(???)  ← Opaque lambda, can't optimize
// - map(???)     ← Don't know what's inside
// - reduceByKey  ← Only this can be optimized

// Can't do:
// - Predicate pushdown
// - Column pruning
// - Join reordering
```

**Problem 3: Verbose and Error-Prone Code**

```scala
// Calculate average age by city - RDD way
val avgAgeByCity = sc.textFile("users.csv")
  .map(_.split(","))
  .filter(_.length == 4)
  .map(f => (f(3), f(2).toInt))  // (city, age)
  .aggregateByKey((0, 0))(
    (acc, age) => (acc._1 + age, acc._2 + 1),
    (acc1, acc2) => (acc1._1 + acc2._1, acc1._2 + acc2._2)
  )
  .map { case (city, (sum, count)) => (city, sum.toDouble / count) }

// 😰 Complex, hard to read, easy to make mistakes
```

**Problem 4: No Cross-Language Consistency**

```python
# Python RDD
userData = sc.textFile("users.csv") \
  .map(lambda line: line.split(",")) \
  .filter(lambda f: int(f[2]) > 18)

# Different API than Scala, different performance characteristics
# Python RDD operations involve serialization overhead
```

#### What We Really Wanted

```
✓ Schema awareness (know column names and types)
✓ Query optimization (like SQL databases do)
✓ Simpler, more readable code
✓ Cross-language consistency
✓ Better performance
```

---

### 1.2 Enter DataFrame: Structured Data with Optimization

In Spark 1.3 (2015), **DataFrames** were introduced to solve these problems.

#### The Same Query - DataFrame Way

```scala
// Much cleaner!
val userData = spark.read
  .option("header", "true")
  .option("inferSchema", "true")
  .csv("users.csv")
  .filter($"age" > 18 && $"age" < 65)

// Benefits:
// ✓ Schema inferred automatically
// ✓ Column names instead of array indices
// ✓ Catalyst optimizer can see the query
// ✓ Readable and maintainable
```

#### Average Age by City - DataFrame Way

```scala
val avgAgeByCity = spark.read
  .option("header", "true")
  .csv("users.csv")
  .groupBy("city")
  .agg(avg("age").as("average_age"))

// Compare to RDD version - much simpler!
```

#### What is a DataFrame?

```mermaid
graph TB
    DF[DataFrame]
    Schema[Schema<br/>StructType]
    Data[Distributed Data<br/>RDD[InternalRow]]
    
    DF --> Schema
    DF --> Data
    
    Schema --> Fields[StructField: name, city, age<br/>Types: String, String, Int]
    Data --> Partitions[Partitions across executors]
    
    style DF fill:#e1f5fe
    style Schema fill:#fff9c4
    style Data fill:#f3e5f5
```

**Definition**: A DataFrame is a distributed collection of data organized into **named columns**, conceptually equivalent to a table in a relational database.

**Under the hood**:
```scala
// DataFrame is actually a type alias
type DataFrame = Dataset[Row]

// Row is a generic object that can hold any schema
case class Row(values: Array[Any])
```

---

### 1.3 Dataset: Type-Safe DataFrames

DataFrames solved many problems, but introduced a new one: **loss of compile-time type safety**.

#### The Type Safety Problem

```scala
// DataFrame - no compile-time checking
val df = spark.read.json("users.json")
df.filter($"age" > 18)  // Compiles fine
df.filter($"agee" > 18) // Also compiles! Runtime error if typo

// No help from IDE
df.select($"nam")  // Compiles, fails at runtime
```

#### Enter Dataset[T] - Best of Both Worlds

In Spark 1.6 (2016), **Datasets** were introduced to add type safety back.

```scala
// Define your domain model
case class User(id: Int, name: String, city: String, age: Int)

// Create typed Dataset
val users: Dataset[User] = spark.read
  .option("header", "true")
  .schema(Encoders.product[User].schema)
  .csv("users.csv")
  .as[User]

// Now we have type safety!
users.filter(_.age > 18)  // ✓ Compile-time checked
users.filter(_.agee > 18) // ✗ Compile error!

users.map(_.name.toUpperCase)  // ✓ IDE autocomplete works
users.map(_.nam)               // ✗ Compile error
```

#### Dataset vs DataFrame

```scala
// DataFrame = Dataset[Row]
val df: DataFrame = spark.read.csv("...")
val row: Row = df.first()  // Row is generic
row.getString(0)  // Access by index, no type safety

// Dataset[User] = typed!
val ds: Dataset[User] = spark.read.csv("...").as[User]
val user: User = ds.first()  // User is your case class
user.name  // Type-safe field access!
```

---

### 1.4 The Complete Evolution

```mermaid
graph LR
    RDD[RDD<br/>Spark 0.x<br/>2012]
    DF[DataFrame<br/>Spark 1.3<br/>2015]
    DS[Dataset<br/>Spark 1.6<br/>2016]
    Unified[Unified APIs<br/>Spark 2.0<br/>2016]
    
    RDD -->|Added schema| DF
    DF -->|Added type safety| DS
    DS --> Unified
    
    style RDD fill:#ffcdd2
    style DF fill:#fff9c4
    style DS fill:#c8e6c9
    style Unified fill:#b3e5fc
```

**Evolution summary**:

| Feature | RDD | DataFrame | Dataset |
|:--------|:----|:----------|:--------|
| **Schema** | ❌ No | ✓ Yes | ✓ Yes |
| **Optimization** | ❌ No | ✓ Yes (Catalyst) | ✓ Yes (Catalyst) |
| **Type Safety** | ⚠️ Partial | ❌ No | ✓ Yes |
| **Readability** | ❌ Verbose | ✓ Good | ✓ Good |
| **Performance** | ⚠️ Good | ✓ Excellent | ✓ Excellent |
| **Cross-language** | ❌ Inconsistent | ✓ Consistent | ⚠️ Scala/Java only |

---

### 1.5 When to Use Each API

```mermaid
graph TB
    Start{What do you need?}
    
    Start -->|Legacy code or<br/>very custom logic| RDD[Use RDD]
    Start -->|SQL-like operations<br/>any language| DF[Use DataFrame]
    Start -->|Type safety +<br/>functional style| DS[Use Dataset]
    
    style RDD fill:#ffcdd2
    style DF fill:#fff9c4
    style DS fill:#c8e6c9
```

**Decision guide**:

**Use RDD when**:
- Working with legacy Spark code
- Need very fine-grained control over partitioning
- Implementing custom domain-specific operations
- Can't express logic with DataFrame/Dataset operations

**Use DataFrame when**:
- SQL-like operations (filter, group, join, aggregate)
- Working in Python or R
- Need maximum optimization from Catalyst
- Working with structured data
- Want cross-language consistency

**Use Dataset when**:
- Working in Scala or Java
- Need compile-time type safety
- Want IDE autocomplete and refactoring support
- Functional programming style preferred
- Complex domain models

**Best practice**: Start with Dataset/DataFrame, fall back to RDD only when necessary.

---

### 1.6 Real-World Example: The Transformation

Let's see a complete example transforming from RDD to DataFrame to Dataset.

#### Problem: Analyze User Purchases

**RDD Approach** (2012 style):

```scala
case class Purchase(userId: Int, productId: String, amount: Double, date: String)

val purchases = sc.textFile("purchases.csv")
  .map(_.split(","))
  .filter(_.length == 4)
  .map(f => Purchase(f(0).toInt, f(1), f(2).toDouble, f(3)))

// Calculate total spend by user
val userSpend = purchases
  .map(p => (p.userId, p.amount))
  .reduceByKey(_ + _)
  .sortBy(_._2, ascending = false)
  .take(10)

// Problems:
// - Manual parsing
// - Verbose
// - No optimization
// - If CSV format changes, runtime errors
```

**DataFrame Approach** (2015 style):

```scala
import org.apache.spark.sql.functions._

val purchases = spark.read
  .option("header", "true")
  .option("inferSchema", "true")
  .csv("purchases.csv")

val userSpend = purchases
  .groupBy("userId")
  .agg(sum("amount").as("totalSpend"))
  .orderBy($"totalSpend".desc)
  .limit(10)

// Benefits:
// ✓ Schema inferred
// ✓ Readable
// ✓ Catalyst optimizes query
// ✓ Can use SQL too
```

**Dataset Approach** (2016 style):

```scala
case class Purchase(userId: Int, productId: String, amount: Double, date: String)

val purchases: Dataset[Purchase] = spark.read
  .option("header", "true")
  .schema(Encoders.product[Purchase].schema)
  .csv("purchases.csv")
  .as[Purchase]

// Type-safe functional API
val userSpend = purchases
  .groupByKey(_.userId)
  .mapGroups { case (userId, purchases) =>
    (userId, purchases.map(_.amount).sum)
  }
  .toDF("userId", "totalSpend")
  .orderBy($"totalSpend".desc)
  .limit(10)

// Benefits:
// ✓ All DataFrame benefits +
// ✓ Compile-time type checking
// ✓ IDE support
```

---

**Key Takeaways from Part 1**:

1. **RDDs were powerful but limited** - no schema, no optimization, verbose
2. **DataFrames added structure and optimization** - schema awareness, Catalyst optimizer
3. **Datasets added type safety** - compile-time checking while keeping optimization
4. **Use the right tool**: Dataset for type safety, DataFrame for SQL-like ops, RDD when necessary

**Next**: Part 2 will dive deep into DataFrame API operations, transformations, and actions.

---

## Part 2: DataFrame API Deep Dive

### 2.1 What IS a DataFrame Internally?

A DataFrame is **not** a simple in-memory table. Understanding its structure is key to using it effectively.

**The Layers**:

```mermaid
graph TB
    User[User Code: df.filter...]
    LogicalPlan[Logical Plan<br/>Abstract query]
    OptimizedPlan[Optimized Logical Plan<br/>Catalyst optimizations]
    PhysicalPlan[Physical Plan<br/>Executable strategy]
    RDD[RDD[InternalRow]<br/>Actual execution]
    
    User --> LogicalPlan
    LogicalPlan --> OptimizedPlan
    OptimizedPlan --> PhysicalPlan
    PhysicalPlan --> RDD
    
    style LogicalPlan fill:#fff9c4
    style OptimizedPlan fill:#c8e6c9
    style PhysicalPlan fill:#b3e5fc
    style RDD fill:#f3e5f5
```

**Under the hood**:
```scala
// DataFrame is a type alias
type DataFrame = Dataset[Row]

// Row is a generic container
sealed trait Row {
  def get(i: Int): Any
  def getString(i: Int): String
  def getInt(i: Int): Int
  // ... other typed getters
}

// InternalRow is Tungsten's binary format (covered later)
```

**Schema is StructType**:
```scala
import org.apache.spark.sql.types._

val schema = StructType(Seq(
  StructField("id", IntegerType, nullable = false),
  StructField("name", StringType, nullable = true),
  StructField("age", IntegerType, nullable = true),
  StructField("city", StringType, nullable = true)
))

// StructField(name, dataType, nullable, metadata)
```

---

### 2.2 Creating DataFrames

#### From Files

**Most common way**:

```scala
// CSV
val csv = spark.read
  .option("header", "true")
  .option("inferSchema", "true")
  .option("sep", ",")
  .csv("users.csv")

// JSON (schema inferred automatically)
val json = spark.read.json("users.json")

//Parquet (schema embedded in file)
val parquet = spark.read.parquet("users.parquet")

// With explicit schema
val users = spark.read
  .schema(schema)
  .csv("users.csv")
```

**Python equivalent**:
```python
# Same API!
users = spark.read \
    .option("header", "true") \
    .option("inferSchema", "true") \
    .csv("users.csv")
```

#### From RDD

```scala
// From RDD of tuples
val rdd = sc.parallelize(Seq(
  (1, "John", 25),
  (2, "Jane", 30)
))

val df = rdd.toDF("id", "name", "age")

// From RDD of case class
case class Person(id: Int, name: String, age: Int)
val peopleRDD = sc.parallelize(Seq(
  Person(1, "John", 25),
  Person(2, "Jane", 30)
))

val df = peopleRDD.toDF()  // Column names from case class
```

#### Programmatically

```scala
import org.apache.spark.sql.Row

val data = Seq(
  Row(1, "John", 25),
  Row(2, "Jane", 30)
)

val df = spark.createDataFrame(
  sc.parallelize(data),
  schema
)
```

#### From SQL

```scala
// Register DataFrame as temp view
users.createOrReplaceTempView("users")

// Query it
val adults = spark.sql("SELECT * FROM users WHERE age >= 18")
```

---

### 2.3 DataFrame Transformations

**All transformations are lazy** - they build up a logical plan.

#### Projections (Selecting Columns)

```scala
// Select specific columns
df.select("name", "age")
df.select($"name", $"age")
df.select(col("name"), col("age"))

// Select with expressions
df.select($"name", $"age" + 1)
df.select(expr("upper(name)"), $"age" * 2)

// selectExpr for SQL strings
df.selectExpr(
  "name",
  "age * 2 as double_age",
  "CASE WHEN age >= 18 THEN 'adult' ELSE 'minor' END as category"
)

// Add columns
df.withColumn("age_plus_one", $"age" + 1)
df.withColumn("full_name", concat($"first_name", lit(" "), $"last_name"))

// Rename columns
df.withColumnRenamed("name", "full_name")

// Drop columns
df.drop("age", "city")
```

#### Filtering (Row Selection)

```scala
// Multiple syntaxes
df.filter($"age" > 18)
df.filter("age > 18")
df.where($"age" > 18)  // alias for filter

// Complex conditions
df.filter($"age" > 18 && $"city" === "NYC")
df.filter("age > 18 AND city = 'NYC'")

// Using Column methods
df.filter($"name".startsWith("J"))
df.filter($"email".contains("@gmail.com"))
df.filter($"age".between(18, 65))
df.filter($"city".isNotNull)
df.filter($"city".isin("NYC", "LA", "SF"))
```

#### Aggregations

**Simple aggregations**:
```scala
import org.apache.spark.sql.functions._

// Single value
df.count()
df.select(avg("age")).show()
df.select(max("age"), min("age")).show()

// Group by
df.groupBy("city").count()

df.groupBy("city").agg(
  avg("age").as("avg_age"),
  max("salary").as("max_salary"),
  count("*").as("num_people")
)

// Multiple columns
df.groupBy("city", "department")
  .agg(avg("salary"))
```

**Available aggregate functions**:
```scala
count, sum, avg, min, max
stddev, variance
first, last
collect_list, collect_set
approx_count_distinct
```

#### Joins

**Join types**:
```scala
users.join(orders, "user_id")  // inner join (default)

users.join(orders, "user_id", "inner")
users.join(orders, "user_id", "left")
users.join(orders, "user_id", "right")
users.join(orders, "user_id", "outer")
users.join(orders, "user_id", "left_semi")  // EXISTS
users.join(orders, "user_id", "left_anti")  // NOT EXISTS
```

**Join with different column names**:
```scala
users.join(orders, users("id") === orders("user_id"))

// Multiple conditions
users.join(orders,
  users("id") === orders("user_id") &&
  users("city") === orders("ship_city")
)
```

**Broadcast join** (for small tables):
```scala
import org.apache.spark.sql.functions.broadcast

large.join(broadcast(small), "key")
// Sends small table to all executors
```

#### Sorting

```scala
df.orderBy("age")
df.orderBy($"age".desc)
df.orderBy($"age".desc, $"name".asc)

df.sort("age")  // alias for orderBy
```

#### Distinct and Deduplication

```scala
// Unique rows
df.distinct()

// Drop duplicates
df.dropDuplicates()

// Drop duplicates by specific columns
df.dropDuplicates("email")
df.dropDuplicates("first_name", "last_name")
```

---

### 2.4 DataFrame Actions

**Actions trigger execution** and return results to driver or write to storage.

```scala
// Collect to driver (careful with large data!)
val rows: Array[Row] = df.collect()

// Show in console (for debugging)
df.show()  // Default 20 rows
df.show(50, truncate = false)

// Count
val count: Long = df.count()

// Take first N rows
val first10: Array[Row] = df.take(10)
val first: Row = df.first()
val head: Row = df.head()

// Write to storage
df.write.parquet("output/users")
df.write.csv("output/users.csv")
df.write.json("output/users.json")

// Write modes
df.write.mode("overwrite").parquet("output")
df.write.mode("append").parquet("output")
df.write.mode("ignore").parquet("output")  // Skip if exists
df.write.mode("errorIfExists").parquet("output")  // Fail if exists

// Partitioning
df.write
  .partitionBy("year", "month")
  .parquet("output")
```

---

### 2.5 Column Expressions and Functions

#### Built-in Functions

**String functions**:
```scala
import org.apache.spark.sql.functions._

df.select(
  upper($"name"),
  lower($"name"),
  length($"name"),
  trim($"name"),
  ltrim($"name"),
  rtrim($"name"),
  substring($"name", 1, 3),
  concat($"first_name", lit(" "), $"last_name"),
  split($"name", " ")
)
```

**Date/Time functions**:
```scala
df.select(
  current_date(),
  current_timestamp(),
  date_add($"date", 7),
  date_sub($"date", 7),
  datediff($"end_date", $"start_date"),
  year($"date"),
  month($"date"),
  dayofmonth($"date"),
  hour($"timestamp")
)
```

**Math functions**:
```scala
df.select(
  abs($"value"),
  round($"value", 2),
  ceil($"value"),
  floor($"value"),
  sqrt($"value"),
  pow($"value", 2),
  rand(),  // Random number
  randn()  // Normal distribution
)
```

**Conditional functions**:
```scala
df.select(
  when($"age" >= 18, "adult")
    .when($"age" >= 13, "teen")
    .otherwise("child").as("category")
)

df.select(
  coalesce($"col1", $"col2", lit("default"))
)
```

#### Window Functions

**Powerful for analytics**:

```scala
import org.apache.spark.sql.expressions.Window

// Define window spec
val windowSpec = Window
  .partitionBy("department")
  .orderBy($"salary".desc)

// Ranking
df.select(
  $"*",
  rank().over(windowSpec).as("rank"),
  dense_rank().over(windowSpec).as("dense_rank"),
  row_number().over(windowSpec).as("row_num")
)

// Running totals
val runningTotal = Window
  .partitionBy("department")
  .orderBy("date")
  .rowsBetween(Window.unboundedPreceding, Window.currentRow)

df.select(
  $"*",
  sum("amount").over(runningTotal).as("running_total")
)

// Moving average
val movingAvg = Window
  .partitionBy("department")
  .orderBy("date")
  .rowsBetween(-6, 0)  // Last 7 days including current

df.select(
  $"*",
  avg("amount").over(movingAvg).as("7day_avg")
)

// Lag/Lead
df.select(
  $"*",
  lag($"amount", 1).over(windowSpec).as("prev_amount"),
  lead($"amount", 1).over(windowSpec).as("next_amount")
)
```

---

### 2.6 Real-World DataFrame Example

**Complete ETL pipeline**:

```scala
// Read sales data from CSV
val sales = spark.read
  .option("header", "true")
  .option("inferSchema", "true")
  .csv("s3://data/sales/*.csv")

// Read product catalog from JSON
val products = spark.read.json("s3://data/products.json")

// Read customer data from Parquet
val customers = spark.read.parquet("s3://data/customers")

// Transform and enrich
val enriched = sales
  // Join with products (broadcast small table)
  .join(broadcast(products), "product_id")
  // Join with customers
  .join(customers, sales("customer_id") === customers("id"))
  // Add derived columns
  .withColumn("revenue", $"quantity" * $"unit_price")
  .withColumn("profit", $"revenue" * $"profit_margin")
  .withColumn("sale_date", to_date($"timestamp"))
  // Filter out invalid records
  .filter($"quantity" > 0 && $"revenue" > 0)
  // Select relevant columns
  .select(
    $"sale_date",
    $"customer_name",
    $"customer_segment",
    $"product_name",
    $"product_category",
    $"quantity",
    $"revenue",
    $"profit"
  )

// Aggregate by category and segment
val summary = enriched
  .groupBy("sale_date", "product_category", "customer_segment")
  .agg(
    sum("revenue").as("total_revenue"),
    sum("profit").as("total_profit"),
    count("*").as("num_transactions"),
    avg("revenue").as("avg_transaction_value"),
    countDistinct("customer_name").as("unique_customers")
  )
  .withColumn("profit_margin", $"total_profit" / $"total_revenue")

// Add rankings
val windowSpec = Window
  .partitionBy("sale_date", "customer_segment")
  .orderBy($"total_revenue".desc)

val ranked = summary
  .withColumn("category_rank", rank().over(windowSpec))
  .filter($"category_rank" <= 10)  // Top 10 per segment

// Write results partitioned by date
ranked.write
  .mode("overwrite")
  .partitionBy("sale_date")
  .parquet("s3://output/sales_summary")

// Also write to data warehouse
ranked.write
  .format("jdbc")
  .option("url", jdbcUrl)
  .option("dbtable", "sales_summary")
  .option("user", user)
  .option("password", password)
  .mode("overwrite")
  .save()
```

**Python version (same logic)**:
```python
from pyspark.sql.functions import *
from pyspark.sql.window import Window

# Read data
sales = spark.read.option("header", "true").csv("s3://data/sales/*.csv")
products = spark.read.json("s3://data/products.json")
customers = spark.read.parquet("s3://data/customers")

# Transform
enriched = (sales
    .join(broadcast(products), "product_id")
    .join(customers, sales.customer_id == customers.id)
    .withColumn("revenue", col("quantity") * col("unit_price"))
    .withColumn("profit", col("revenue") * col("profit_margin"))
    .withColumn("sale_date", to_date("timestamp"))
    .filter((col("quantity") > 0) & (col("revenue") > 0))
)

# Aggregate
summary = (enriched
    .groupBy("sale_date", "product_category", "customer_segment")
    .agg(
        sum("revenue").alias("total_revenue"),
        sum("profit").alias("total_profit"),
        count("*").alias("num_transactions")
    )
)

# Write
summary.write.partitionBy("sale_date").parquet("s3://output/sales_summary")
```

---

**Key Takeaways from Part 2**:

1. **DataFrames are multi-layered** - logical plan → optimized plan → physical plan → RDD
2. **Multiple ways to create** - files, RDD, SQL, programmatic
3. **Rich transformation API** - projections, filters, joins, aggregations, windows
4. **Actions trigger execution** - show, collect, count, write
5. **Built-in functions are optimized** - use them instead of UDFs when possible
6. **Window functions are powerful** - rankings, running totals, moving averages
7. **APIs are consistent** - same across Scala, Python, Java

**Next**: Part 3 will cover Dataset API with type safety and encoders.

---

## Part 3: Dataset API Deep Dive

### 3.1 What IS a Dataset[T]?

A **Dataset** is a strongly-typed collection of domain-specific objects that can be transformed using functional or relational operations.

**The key difference**:
```scala
// DataFrame = loosely typed
val df: DataFrame = spark.read.json("users.json")
val row: Row = df.first()
row.getString(0)  // Access by index, runtime type checking

// Dataset = strongly typed
case class User(id: Int, name: String, age: Int)
val ds: Dataset[User] = spark.read.json("users.json").as[User]
val user: User = ds.first()
user.name  // Compile-time type checking!
```

**Type hierarchy**:
```mermaid
graph TB
    Dataset["Dataset[T]<br/>(Generic)"]
    DatasetUser["Dataset[User]"]
    DatasetProd["Dataset[Product]"]
    DataFrame["DataFrame<br/>(Dataset[Row])"]
    
    Dataset --> DatasetUser
    Dataset --> DatasetProd
    Dataset --> DataFrame
    
    style Dataset fill:#c8e6c9
    style DataFrame fill:#fff9c4
```

---

### 3.2 Creating Datasets

#### From Case Classes

```scala
case class Person(id: Int, name: String, age: Int, city: String)

// From sequence
val people = Seq(
  Person(1, "John", 25, "NYC"),
  Person(2, "Jane", 30, "LA"),
  Person(3, "Bob", 35, "SF")
)

val ds: Dataset[Person] = people.toDS()
```

#### From DataFrame

```scala
// Read as DataFrame
val df = spark.read
  .option("header", "true")
  .csv("people.csv")

// Convert to Dataset
val ds: Dataset[Person] = df.as[Person]

// ds now has compile-time type checking!
```

#### From RDD

```scala
val rdd = sc.parallelize(people)
val ds = rdd.toDS()
```

#### Specifying Schema

```scala
import org.apache.spark.sql.Encoders

val ds = spark.read
  .schema(Encoders.product[Person].schema)
  .json("people.json")
  .as[Person]
```

---

### 3.3 Typed vs Untyped Operations

Datasets support **both** typed (functional) and untyped (relational) operations.

#### Typed Operations (Compile-Time Safe)

```scala
val ds: Dataset[Person] = ...

// filter with lambda
ds.filter(_.age > 18)  // ✓ Compile-time type check

ds.filter(_.agee > 18) // ✗ Compile error: value agee is not a member

// map to new type
val names: Dataset[String] = ds.map(_.name)

val ages: Dataset[Int] = ds.map(_.age)

// flatMap
val words: Dataset[String] = ds.flatMap(p => p.name.split(" "))

// groupByKey (typed aggregation)
ds.groupByKey(_.city)
  .count()
  // Returns: Dataset[(String, Long)]
```

#### Untyped Operations (Runtime Checked)

```scala
// Same DataFrame operations work
ds.select($"name", $"age")  // Returns DataFrame

ds.filter($"age" > 18)  // Returns DataFrame

ds.groupBy("city").count()  // Returns DataFrame

// No compile-time checking for column names!
ds.select($"agee")  // Compiles, runtime error
```

**When to use which**:

| Use Case | Typed API | Untyped API |
|:---------|:----------|:------------|
| **Filter by field** | `ds.filter(_.age > 18)` | `ds.filter($"age" > 18)` |
| **Transform** | `ds.map(_.name.toUpperCase)` | `ds.select(upper($"name"))` |
| **Aggregation** | `ds.groupByKey(_.city).count()` | `ds.groupBy("city").count()` |
| **When to use** | Complex logic, type safety needed | SQL-like operations, optimization priority |

---

### 3.4 Encoders: The Secret Sauce

**Encoders** are the mechanism that converts JVM objects to Spark's internal binary format (Tungsten).

#### What are Encoders?

```mermaid
graph LR
    JVM["JVM Object<br/>(Person)"]
    Encoder[Encoder]
    Tungsten["Tungsten Binary<br/>(Compact, Fast)"]
    Decoder[Decoder]
    
    JVM -->|Serialize| Encoder
    Encoder --> Tungsten
    Tungsten -->|Deserialize| Decoder
    Decoder --> JVM
    
    style Tungsten fill:#b3e5fc
```

**Why Encoders matter**:

**Without Encoders (RDD with Java serialization)**:
```scala
case class Person(id: Int, name: String, age: Int, email: String, address: String)

rdd.map(_.age).sum()

// Java serialization:
// - Serializes ENTIRE Person object for each operation
// - Large overhead (object headers, class info)
// - Slow serialization/deserialization
```

**With Encoders (Dataset with Tungsten)**:
```scala
ds.map(_.age).sum()

// Encoder serialization:
// - Only extracts 'age' field (4 bytes)
// - Binary format (no object overhead)
// - Fast serialization/deserialization
```

**Performance comparison**:
```
Java Serialization: ~500 MB for 1M Person objects
Tungsten Format:    ~100 MB for same data (5x smaller!)

Serialization time: 10x faster with Encoders
```

#### Implicit Encoders

Spark provides implicit encoders for:

```scala
// Primitives
implicitly[Encoder[Int]]
implicitly[Encoder[Long]]
implicitly[Encoder[String]]
implicitly[Encoder[Boolean]]

// Tuples (up to 22 elements)
implicitly[Encoder[(String, Int)]]
implicitly[Encoder[(String, Int, Double)]]

// Case classes (via scala reflection)
case class Person(name: String, age: Int)
implicitly[Encoder[Person]]  // Generated automatically!

// Collections
implicitly[Encoder[Array[String]]]
implicitly[Encoder[Seq[Int]]]
implicitly[Encoder[Map[String, Int]]]
```

#### Custom Encoders

For complex types, you may need custom encoders:

```scala
import org.apache.spark.sql.Encoders

// Explicit encoders
Encoders.INT
Encoders.STRING
Encoders.product[Person]  // For case classes
Encoders.tuple(Encoders.scalaInt, Encoders.STRING)

// For Java beans
Encoders.bean(classOf[JavaPerson])
```

#### The Tungsten Binary Format

**Example**: How `Person(1, "John", 25)` is encoded:

```
JVM Object in Memory:
----------------------------------------
| Object Header (12 bytes)             |
| Int id = 1 (4 bytes)                 |
| String name ref → Heap (8 bytes)     |
|   → String object (24+ bytes)        |
| Int age = 25 (4 bytes)               |
----------------------------------------
Total: ~52 bytes

Tungsten Binary Format:
----------------------------------------
| id: 00 00 00 01 (4 bytes)            |
| name length: 04 (4 bytes)            |
| name data: "John" (4 bytes)          |
| age: 00 00 00 19 (4 bytes)           |
----------------------------------------
Total: 16 bytes (3x smaller!)
```

**Benefits**:
- ✓ Compact (no object headers)
- ✓ Cache-friendly (contiguous memory)
- ✓ Fast serialization (no reflection)
- ✓ GC-friendly (off-heap storage possible)

---

### 3.5 Type-Safe Transformations

#### map, flatMap, filter

```scala
case class Person(name: String, age: Int, city: String)
val ds: Dataset[Person] = ...

// map: 1-to-1 transformation
val names: Dataset[String] = ds.map(_.name)

val older: Dataset[Person] = ds.map(p => 
  p.copy(age = p.age + 1)
)

// flatMap: 1-to-many
val nameWords: Dataset[String] = ds.flatMap(p => p.name.split(" "))

// filter: predicate
val adults: Dataset[Person] = ds.filter(_.age >= 18)

val nycPeople: Dataset[Person] = ds.filter(_.city == "NYC")
```

#### Type-safe joins

```scala
case class Order(userId: Int, amount: Double, product: String)
val users: Dataset[Person] = ...
val orders: Dataset[Order] = ...

// Join and get typed result
val joined: Dataset[(Person, Order)] = users
  .joinWith(orders, users("id") === orders("userId"))

// Access with tuple syntax
joined.map { case (person, order) =>
  s"${person.name} bought ${order.product} for $${order.amount}"
}
```

---

### 3.6 Type-Safe Aggregations

**Using TypedColumn**:

```scala
import org.apache.spark.sql.expressions.scalalang.typed

case class Sale(product: String, category: String, amount: Double, quantity: Int)
val sales: Dataset[Sale] = ...

// Typed aggregations
val result = sales
  .groupByKey(_.category)
  .agg(
    typed.sum[Sale](_.amount).name("total_sales"),
    typed.avg[Sale](_.quantity).name("avg_quantity"),
    typed.count[Sale](_.product).name("num_products")
  )

// Result type: Dataset[(String, Double, Double, Long)]
// (category, total_sales, avg_quantity, num_products)
```

**mapGroups for complex aggregations**:

```scala
// Custom aggregation logic
val categoryStats = sales
  .groupByKey(_.category)
  .mapGroups { case (category, sales) =>
    val salesList = sales.toList
    val totalAmount = salesList.map(_.amount).sum
    val totalQuantity = salesList.map(_.quantity).sum
    val avgPrice = totalAmount / totalQuantity
    
    (category, totalAmount, totalQuantity, avgPrice)
  }

// Returns: Dataset[(String, Double, Int, Double)]
```

**reduceGroups**:

```scala
val maxSaleByCategory = sales
  .groupByKey(_.category)
  .reduceGroups((s1, s2) => 
    if (s1.amount > s2.amount) s1 else s2
  )

// Returns: Dataset[(String, Sale)]
```

---

### 3.7 Converting Between DataFrame and Dataset

**DataFrame → Dataset**:
```scala
val df: DataFrame = spark.read.json("people.json")

// Explicit conversion
val ds: Dataset[Person] = df.as[Person]

// With renamed columns
val ds2 = df
  .withColumnRenamed("person_id", "id")
  .as[Person]
```

**Dataset → DataFrame**:
```scala
val ds: Dataset[Person] = ...

// Convert to DataFrame
val df: DataFrame = ds.toDF()

// Access as Row
val row: Row = df.first()
```

**Best practice**: Mix both APIs!

```scala
// Start typed
val ds: Dataset[Person] = spark.read.json("...").as[Person]

// Use DataFrame for SQL operations (better optimized)
val filtered = ds.toDF()
  .filter($"age" > 18)
  .groupBy("city").agg(avg("age"))

// Back to typed for complex logic
case class CityStats(city: String, avgAge: Double)
val stats: Dataset[CityStats] = filtered.as[CityStats]

stats.map(s => s.copy(avgAge = s.avgAge.round))
```

---

### 3.8 Real-World Dataset Example

**Problem**: Process user clickstream data with complex business logic.

```scala
case class ClickEvent(
  userId: Int,
  timestamp: Long,
  page: String,
  action: String,
  duration: Int
)

case class UserSession(
  userId: Int,
  sessionId: String,
  startTime: Long,
  endTime: Long,
  pages: Seq[String],
  totalDuration: Int
)

val events: Dataset[ClickEvent] = spark.read
  .json("clickstream")
  .as[ClickEvent]

// Complex typed transformation
def createSessions(events: Seq[ClickEvent]): Seq[UserSession] = {
  val sortedEvents = events.sortBy(_.timestamp)
  val sessionGap = 30 * 60 * 1000 // 30 minutes
  
  sortedEvents.foldLeft(List.empty[(Int, List[ClickEvent])]) {
    case (sessions, event) =>
      sessions match {
        case Nil =>
          List((1, List(event)))
        case (sessionId, currentSession) :: rest =>
          val lastTimestamp = currentSession.head.timestamp
          if (event.timestamp - lastTimestamp < sessionGap) {
            // Same session
            (sessionId, event :: currentSession) :: rest
          } else {
            // New session
            (sessionId + 1, List(event)) :: sessions
          }
      }
  }.map { case (sessionId, events) =>
    UserSession(
      userId = events.head.userId,
      sessionId = s"${events.head.userId}_$sessionId",
      startTime = events.last.timestamp,
      endTime = events.head.timestamp,
      pages = events.reverse.map(_.page),
      totalDuration = events.map(_.duration).sum
    )
  }
}

// Apply complex logic with type safety
val sessions: Dataset[UserSession] = events
  .groupByKey(_.userId)
  .flatMapGroups { case (userId, events) =>
    createSessions(events.toSeq)
  }

// Continue with typed operations
val longSessions = sessions
  .filter(_.totalDuration > 300)  // > 5 minutes
  .filter(_.pages.size >= 3)       // At least 3 pages

// Aggregate
val userStats = longSessions
  .groupByKey(_.userId)
  .agg(
    typed.count[UserSession](_.sessionId).name("total_sessions"),
    typed.avg[UserSession](_.totalDuration.toDouble).name("avg_duration")
  )
```

---

**Key Takeaways from Part 3**:

1. **Datasets = DataFrame + Type Safety** - Best of both worlds
2. **Encoders are critical** - Tungsten binary format for performance
3. **Two operation styles** - Typed (compile-time safe) and untyped (SQL-like)
4. **Use typed for complex logic** - When you need IDE support and type checking
5. **Use DataFrame for SQL operations** - Better Catalyst optimization
6. **Mix both APIs** - Switch between Dataset and DataFrame as needed
7. **Type-safe aggregations** - TypedColumn for compile-time checked aggregations

**Next**: Part 4 will cover Spark SQL and UDFs in detail.

---

## Part 4: Spark SQL Deep Dive

### 4.1 The SQL Interface

Spark SQL allows you to query DataFrames using SQL syntax - the same queries work on structured data!

#### Registering Temporary Views

```scala
val df = spark.read.parquet("users.parquet")

// Create temporary view (session-scoped)
df.createOrReplaceTempView("users")

// Now query with SQL
val adults = spark.sql("""
  SELECT name, age, city
  FROM users
  WHERE age >= 18
""")

// Global views (accessible across sessions)
df.createGlobalTempView("users_global")
spark.sql("SELECT * FROM global_temp.users_global")
```

**Python**:
```python
df.createOrReplaceTempView("users")
adults = spark.sql("SELECT * FROM users WHERE age >= 18")
```

#### Complex SQL Queries

```sql
-- CTEs (Common Table Expressions)
WITH high_value_customers AS (
  SELECT customer_id, SUM(amount) as total_spend

FROM orders
  GROUP BY customer_id
  HAVING total_spend > 10000
)
SELECT c.name, c.email, h.total_spend
FROM customers c
JOIN high_value_customers h ON c.id = h.customer_id
ORDER BY h.total_spend DESC

-- Window functions in SQL
SELECT 
  employee_id,
  department,
  salary,
  RANK() OVER (PARTITION BY department ORDER BY salary DESC) as dept_rank,
  AVG(salary) OVER (PARTITION BY department) as dept_avg_salary
FROM employees

-- Subqueries
SELECT name, age
FROM users
WHERE age > (SELECT AVG(age) FROM users)
```

---

### 4.2 User-Defined Functions (UDFs)

When built-in functions aren't enough, create custom UDFs.

#### Scalar UDFs

```scala
import org.apache.spark.sql.functions.udf

// Define Scala function
def calculateBonus(salary: Double, rating: Int): Double = {
  salary * rating * 0.1
}

// Register as UDF for DataFrame API
val bonusUDF = udf(calculateBonus _)

df.select($"name", bonusUDF($"salary", $"rating").as("bonus"))

// Register for SQL
spark.udf.register("calculateBonus", calculateBonus _)

spark.sql("""
  SELECT name, calculateBonus(salary, rating) as bonus
  FROM employees
""")
```

**Python UDF**:
```python
from pyspark.sql.functions import udf
from pyspark.sql.types import DoubleType

def calculate_bonus(salary, rating):
    return salary * rating * 0.1

bonus_udf = udf(calculate_bonus, DoubleType())
df.select("name", bonus_udf("salary", "rating").alias("bonus"))
```

#### UDAF (User-Defined Aggregate Functions)

**NOTE**: UDAFs are deprecated in Spark 3.x. Use Aggregator instead.

**Aggregator (type-safe)**:
```scala
import org.apache.spark.sql.{Encoder, Encoders}
import org.apache.spark.sql.expressions.Aggregator

case class Average(var sum: Double, var count: Long)

object MyAverage extends Aggregator[Double, Average, Double] {
  def zero: Average = Average(0.0, 0L)
  
  def reduce(buffer: Average, data: Double): Average = {
    buffer.sum += data
    buffer.count += 1
    buffer
  }
  
  def merge(b1: Average, b2: Average): Average = {
    b1.sum += b2.sum
    b1.count += b2.count
    b1
  }
  
  def finish(reduction: Average): Double = {
    reduction.sum / reduction.count
  }
  
  def bufferEncoder: Encoder[Average] = Encoders.product
  def outputEncoder: Encoder[Double] = Encoders.scalaDouble
}

// Use it
import org.apache.spark.sql.functions._
val myAvg = MyAverage.toColumn.name("avg")
df.select(myAvg)
```

---

## Part 5: The Catalyst Optimizer

**This is the secret sauce that makes Spark SQL/DataFrame fast!**

### 5.1 The Four-Phase Optimization Pipeline

```mermaid
graph TB
    SQL[SQL Query or<br/>DataFrame code]
    UnresolvedPlan[1. Analysis<br/>Unresolved Logical Plan]
    LogicalPlan[2. Logical Optimization<br/>Optimized Logical Plan]
    PhysicalPlan[3. Physical Planning<br/>Selected Physical Plan]
    RDD[4. Code Generation<br/>RDD of compiled code]
    
    SQL --> UnresolvedPlan
    UnresolvedPlan --> LogicalPlan
    LogicalPlan --> PhysicalPlan
    PhysicalPlan --> RDD
    
    style UnresolvedPlan fill:#fff9c4
    style LogicalPlan fill:#c8e6c9
    style PhysicalPlan fill:#b3e5fc
    style RDD fill:#f3e5f5
```

---

### 5.2 Phase 1: Analysis

**Convert unresolved names to actual columns**:

```scala
users.filter($"age" > 18).select("name")
```

**Unresolved Plan**:
```
Project [name#?]
└── Filter (age#? > 18)
    └── UnresolvedRelation [users]
```

**After Analysis** (resolved):
```
Project [name#2]
└── Filter (age#3 > 18)
    └── Relation [id#1, name#2, age#3, city#4]
        Schema: (IntegerType, StringType, IntegerType, StringType)
```

**What happens**:
- Resolve column names from catalog
- Resolve table names
- Check types match
- Assign unique IDs to columns

---

### 5.3 Phase 2: Logical Optimization

**Rule-based optimizations** that transform the logical plan.

#### Predicate Pushdown

**BEFORE**:
```scala
users.join(orders, "user_id")
  .filter($"age" > 18)
```

**Unoptimized plan**:
```
Filter (age > 18)
└── Join (user_id)
    ├── Relation [users]
    └── Relation [orders]
    
Steps: 1. Join all users × orders (huge!)
       2. Filter result
```

**AFTER** optimization:
```
Join (user_id)
├── Filter (age > 18)
│   └── Relation [users]
└── Relation [orders]

Steps: 1. Filter users first (smaller!)
       2. Join filtered users × orders
```

**Why it matters**:
- Join 1M users × 10M orders = 10 trillion rows
- Filter first: 100K users (age > 18) × 10M orders = 1 billion rows
- **10,000x fewer rows to process!**

#### Column Pruning

```scala
spark.read.parquet("users")  // Has 50 columns
  .select("name", "age")
```

**Optimized**:
```
Only reads 'name' and 'age' columns from Parquet
Skips other 48 columns entirely
```

**With Parquet/ORC (columnar formats)**:
- Only read bytes for requested columns
- Can save 95%+ I/O!

#### Constant Folding

```scala
df.filter($"age" > 10 + 8)
```

**Optimized**:
```
df.filter($"age" > 18)  // Computed at compile time
```

#### Boolean Expression Simplification

```scala
df.filter(($"age" > 18) && ($"age" > 10))
```

**Optimized**:
```
df.filter($"age" > 18)  // Second condition redundant
```

---

### 5.4 Phase 3: Physical Planning

**Choose the best physical execution strategy**.

#### Join Strategy Selection

```scala
large.join(small, "key")
```

**Catalyst evaluates**:

```mermaid
graph TB
    Join[Join Strategy?]
    Stats{Table Statistics}
    
    Stats -->|small < 10MB| Broadcast[Broadcast Hash Join]
    Stats -->|both large| Sort[Sort Merge Join]
    Stats -->|one side very small| Hash[Shuffle Hash Join]
    
    Join --> Stats
    
    style Broadcast fill:#c8e6c9
    style Sort fill:#fff9c4
    style Hash fill:#b3e5fc
```

**Broadcast Hash Join** (small < 10MB default):
```
1. Broadcast small table to all executors
2. Hash join locally (no shuffle!)
Pro: Fast, no shuffle
Con: Only works for small tables
```

**Sort Merge Join** (both large):
```
1. Shuffle both tables by join key
2. Sort both sides
3. Merge sorted partitions
Pro: Scalable for large-large joins
Con: Requires shuffle + sort
```

**Cost-Based Optimization** (CBO):

```sql
-- Collect table statistics
ANALYZE TABLE users COMPUTE STATISTICS
ANALYZE TABLE orders COMPUTE STATISTICS

-- Catalyst uses stats to choose best plan
SELECT * 
FROM users u 
JOIN orders o ON u.id = o.user_id
JOIN products p ON o.product_id = p.id

-- CBO considers:
-- - Table sizes
-- - Join selectivity
-- - Column statistics (min, max, distinct count)
-- - Chooses optimal join order automatically
```

---

### 5.5 Phase 4: Code Generation (Tungsten)

**Whole-Stage Code Generation** - Java code generated and JIT-compiled.

**Without Code Generation** (iterator-based):
```scala
rdd.map(r => r.age + 1)
   .filter(age => age > 18)
   .map(age => age * 2)
```

**Creates 3 iterators**:
```java
// Pseudocode
for (Row row : input) {
    int age1 = row.getInt(ageIndex) + 1;  // Iterator 1
    iterator1.add(age1);
}
for (int age : iterator1) {
    if (age > 18) {  // Iterator 2
        iterator2.add(age);
    }
}
for (int age : iterator2) {
    int doubled = age * 2;  // Iterator 3
    output.add(doubled);
}
```

**With Whole-Stage Code Generation**:
```java
// Single fused loop!
for (Row row : input) {
    int age = row.getInt(ageIndex) + 1;
    if (age > 18) {
        output[i++] = age * 2;
    }
}
// JIT-compiled to machine code
```

**Performance**:
```
Iterator-based: 100M rows/sec
Whole-stage codegen: 1B rows/sec (10x faster!)
```

---

## Part 6: Best Practices & Performance

### 6.1 API Selection Guide

**Use DataFrame when**:
- ✓ SQL-like operations
- ✓ Working in Python/R
- ✓ Want maximum Catalyst optimization
- ✓ Cross-language compatibility needed

**Use Dataset when**:
- ✓ Need type safety (Scala/Java)
- ✓ Complex domain logic
- ✓ IDE autocomplete important
- ✓ Compile-time error checking

**Use RDD when**:
- ✓ Very custom partitioning logic
- ✓ Reading non-standard formats
- ✓ Can't express with DataFrame/Dataset

---

### 6.2 Performance Tips

#### 1. Use Built-in Functions over UDFs

```scala
// ❌ AVOID: UDF (opaque to Catalyst)
val upper = udf((s: String) => s.toUpperCase)
df.select(upper($"name"))
// Catalyst can't optimize this!

// ✅ PREFER: Built-in function
df.select(upper($"name"))
// Catalyst can push down, optimize, codegen
```

**Performance difference**:
- Built-in: 10x faster
- UDF: Python UDF can be 100x slower!

#### 2. Partition Pruning

```scala
// Write with partitioning
df.write
  .partitionBy("year", "month", "day")
  .parquet("events")

// Read with filter → partition pruning!
spark.read.parquet("events")
  .filter($"year" === 2024 && $"month" === 1)
// Only reads Jan 2024 partitions!
```

#### 3. Broadcast Small Tables

```scala
import org.apache.spark.sql.functions.broadcast

// Force broadcast (if you know it's small)
large.join(broadcast(small), "key")

// Adjust threshold
spark.conf.set("spark.sql.autoBroadcastJoinThreshold", 50 * 1024 * 1024) // 50MB
```

#### 4. Cache Wisely

```scala
val filtered = df.filter($"active" === true)

// ❌ Bad: compute twice
filtered.count()
filtered.show()

// ✅ Good: cache if reused
val filtered = df.filter($"active" === true).cache()
filtered.count()  // Materializes cache
filtered.show()   // Uses cache
```

**Cache strategy**:
```scala
// MEMORY_ONLY (default)
df.cache()

// MEMORY_AND_DISK (spills to disk)
df.persist(StorageLevel.MEMORY_AND_DISK)

// DISK_ONLY
df.persist(StorageLevel.DISK_ONLY)
```

#### 5. Avoid Collect on Large Data

```scala
// ❌ DANGER: Pulls ALL data to driver
val data = df.collect()  // OOM if df is large!

// ✅ Use take/limit
val sample = df.take(1000)

// ✅ Or aggregate
val stats = df.select(avg("age"), max("salary")).first()

// ✅ Or write to storage
df.write.parquet("output")
```

---

### 6.3 Common Anti-Patterns

#### ❌ Repeated Non-Cached Computations

```scala
val result = df.filter(...).groupBy(...).agg(...)

result.count()  // Full computation
result.write.parquet("out1")  // Recomputed!
result.show()  // Recomputed again!

// ✅ Fix: Cache it
result.cache()
result.count()  // Computation + cache
result.write.parquet("out1")  // From cache
```

#### ❌ Not Using Partitioning for Writes

```scala
// Bad: Single huge file
df.write.parquet("output")

// Good: Partitioned by common filter columns
df.write.partitionBy("date", "region").parquet("output")
```

#### ❌ UDF for Simple Operations

```scala
// Bad
val lengthUDF = udf((s: String) => s.length)
df.select(lengthUDF($"name"))

// Good
df.select(length($"name"))
```

---

## Part 7: Real-World Complete Example

### 7.1 E-Commerce Analytics Pipeline

```scala
import org.apache.spark.sql.functions._
import org.apache.spark.sql.expressions.Window

// 1. READ: Multiple sources
val transactions = spark.read
  .option("mergeSchema", "true")
  .parquet("s3://data/transactions/")

val products = spark.read
  .json("s3://catalog/products.json")

val customers = spark.read
  .format("jdbc")
  .option("url", jdbcUrl)
  .option("dbtable", "customers")
  .load()

// 2. TRANSFORM: Enrich data
val enriched = transactions
  // Small table broadcast
  .join(broadcast(products), "product_id")
  .join(customers, transactions("customer_id") === customers("id"))
  // Derived columns
  .withColumn("revenue", $"quantity" * $"price")
  .withColumn("profit", $"revenue" * $"margin")
  .withColumn("date", to_date($"timestamp"))
  // Data quality
  .filter($"quantity" > 0 && $"price" > 0)
  .dropDuplicates("transaction_id")

// 3. AGGREGATE: Business metrics
val dailySummary = enriched
  .groupBy("date", "category", "customer_segment")
  .agg(
    sum("revenue").as("total_revenue"),
    sum("profit").as("total_profit"),
    count("*").as("num_transactions"),
    countDistinct("customer_id").as("unique_customers"),
    avg("revenue").as("avg_transaction_value")
  )
  .withColumn("profit_margin_pct", 
    ($"total_profit" / $"total_revenue" * 100).cast("decimal(5,2)"))

// 4. RANK: Top categories
val windowSpec = Window
  .partitionBy("date", "customer_segment")
  .orderBy($"total_revenue".desc)

val topCategories = dailySummary
  .withColumn("rank", dense_rank().over(windowSpec))
  .filter($"rank" <= 10)

// 5. CACHE: Will be reused
topCategories.cache()

// 6. WRITE: Multiple outputs
// Parquet for data lake
topCategories.write
  .mode("overwrite")
  .partitionBy("date")
  .parquet("s3://warehouse/daily_summary")

// CSV for reports
topCategories
  .coalesce(1)  // Single file
  .write
  .mode("overwrite")
  .option("header", "true")
  .csv("s3://reports/top_categories")

// Database for dashboards
topCategories.write
  .format("jdbc")
  .option("url", warehouseUrl)
  .option("dbtable", "daily_summary")
  .mode("overwrite")
  .save()
```

---

**Document Status**: ✅ Complete!

**What You Learned**:

1. **RDD → DataFrame → Dataset Evolution** - Why each API exists
2. **DataFrame API Mastery** - Transformations, actions, built-in functions
3. **Dataset Type Safety** - Encoders, typed operations, performance
4. **Spark SQL** - SQL interface, UDFs, mixing SQL and DataFrame
5. **Catalyst Optimizer** - 4-phase pipeline, predicate pushdown, code generation
6. **Best Practices** - API selection, performance tips, anti-patterns
7. **Real-World** - Complete ETL pipeline example

**Further Reading**:
- Official Spark SQL Guide
- Catalyst paper (2015)
- Tungsten blog posts
- DataFrame vs RDD benchmarks

---
