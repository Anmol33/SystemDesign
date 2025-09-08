# The Ultimate Guide to Apache Parquet: A Deep Dive Into the Columnar Powerhouse

**Introduction**

In the world of big data, performance is paramount. How you store your data directly impacts how quickly you can query and analyze it. For years, formats like CSV dominated, but as datasets grew into terabytes and petabytes, a more efficient solution was needed. Enter Apache Parquet.

Parquet is an open-source, columnar storage file format designed for efficient data storage and retrieval. It provides high-performance compression and encoding schemes to handle complex data in bulk, making it a foundational technology for analytics in the modern data stack.

This guide will walk you through everything you need to know about Parquet—from its core concepts to its internal architecture and why it's the preferred choice for data lakes and large-scale data processing.

---

### **Why Not Just Use CSV? The Power of Columnar Storage**

To understand Parquet, you first need to understand the difference between row-based and column-based storage.

**Row-based formats (like CSV)** store data sequentially, row by row. This is great for transactional systems where you often need to read or write an entire record at once.

*Example of a CSV file:*

Code snippet

id,name,country  
1,Alice,USA  
2,Bob,Canada  
3,Charlie,USA

To find the average age from a billion-record CSV file, you would have to read the entire file, line by line, just to pick out the age value from each row.

**Columnar formats (like Parquet)** flip this on its head. They store all the values for a single column together.

*How Parquet conceptually organizes the same data:*

File:  
  Column 'id':      \[1, 2, 3\]  
  Column 'name':    \["Alice", "Bob", "Charlie"\]  
  Column 'country': \["USA", "Canada", "USA"\]

Now, if you want to find the average age, the system only needs to read the age data block. It can completely ignore the id and name columns, drastically reducing the amount of data read from disk (I/O) and speeding up analytical queries. This is known as **column pruning**.

#### **Key Advantages of Columnar Storage:**

1. **I/O Efficiency**: Queries that only need a subset of columns don't have to read the entire dataset.  
2. **Higher Compression Ratios**: Since data in a column is of the same type (e.g., all integers or all strings), it's highly compressible. Similar values often appear together, which algorithms like Run-Length Encoding and Dictionary Encoding can exploit.  
3. **Predicate Pushdown**: Parquet stores statistics (min, max, count) for chunks of data. A query engine can use this metadata to skip reading entire chunks of data if they don't contain the data needed, a process called **predicate pushdown**. For example, if a query asks for sales \> 500 and a data block's metadata says its maximum value is 450, the entire block can be skipped.

---

### **The Anatomy of a Parquet File**

A Parquet file isn't just a jumble of compressed columns; it’s a highly organized structure designed for performance and scalability. A single Parquet file can be logically broken down into several key components.

\+-------------------------------------------------+  
|               4-byte Magic Number ("PAR1")      |  \<- Header  
\+-------------------------------------------------+  
|                  Row Group 1                    |  
|                (Column Chunk 1\)                 |  
|                (Column Chunk 2\)                 |  
|                ( ... )                          |  
\+-------------------------------------------------+  
|                  Row Group 2                    |  
|                (Column Chunk 1\)                 |  
|                (Column Chunk 2\)                 |  
|                ( ... )                          |  
\+-------------------------------------------------+  
|                       ...                       |  
\+-------------------------------------------------+  
|                    File Footer                  |  \<- Metadata is here  
|                   (FileMetaData)                |  
|           (Schema, Row Group Locations)         |  
|              (Footer Length: 4 bytes)           |  
|               4-byte Magic Number ("PAR1")      |  
\+-------------------------------------------------+

#### **1\. File Header & Footer: The Bookends**

Every Parquet file starts and ends with a 4-byte magic number, PAR1, to identify it as a Parquet file. The real intelligence, however, is in the **File Footer**.

The footer is written at the very end of the file and acts as its index or table of contents. It contains the **FileMetaData**, which holds:

* **Schema**: The complete structure of the data, including column names, data types, and nesting levels.  
* **Row Group Metadata**: Pointers (offsets) to the exact location of each **Column Chunk** within the file, along with their sizes and statistics (min/max values).  
* **Pointers to Page Indexes**: Locations for the Column Index and Offset Index, which provide even more granular statistics for skipping data.  
* **File-level Metadata**: Additional custom key-value metadata.

By placing this metadata at the end, Parquet files can be created in a **single pass**. Data can be streamed to disk, and only when all the data is written is the final metadata block appended. This is perfect for distributed systems where data is processed in chunks.

#### **2\. Row Groups: The Horizontal Slice**

A large dataset within a Parquet file is broken down into horizontal chunks called **Row Groups**. Think of a 1-billion-row table being split into ten Row Groups of 100 million rows each. This allows parallel processing engines like Spark to assign different Row Groups to different worker nodes, enabling massive scalability.

#### **3\. Column Chunks: The Vertical Slice**

Within each Row Group, the data is stored column-by-column in **Column Chunks**. If you have columns for user\_id, product\_id, and price, each row group will contain a separate chunk for each of these columns.

#### **4\. Pages: The Atomic Unit of Data**

Each Column Chunk is further subdivided into **Pages**. A Page is the smallest atomic unit of data that is encoded and compressed. It typically contains a few thousand values from a single column.

This granular structure is key to Parquet’s efficiency:

* **Compression**: Each page is compressed individually. Since all values in a page are of the same data type, compression algorithms (like Snappy, Gzip, or Zstd) work exceptionally well.  
* **Encoding**: Parquet uses specialized encoding techniques to reduce data size even before compression.  
  * **Dictionary Encoding**: For columns with low cardinality (few unique values), like a country column. Instead of storing "United States" thousands of times, it stores a small integer ID and a dictionary mapping the ID to the full string.  
  * **Run-Length Encoding (RLE)**: For sequences of repeating values, it stores the value and the repeat count. \[5, 5, 5, 5, 5\] becomes (value: 5, count: 5\).  
  * **Delta Encoding**: For sequences of numbers that increase or decrease steadily (like timestamps or IDs), it stores only the difference (delta) from the previous value, resulting in smaller numbers that are easier to compress.  
* **Page-Level Statistics**: Each data page contains statistics (min/max values). This allows for even finer-grained predicate pushdown, where a query engine can skip reading individual pages within a column chunk if the data isn't relevant.

---

### **Inside a Page: The Smallest Unit of Work**

To truly appreciate Parquet, we need to zoom in on the page itself. Each page is a self-contained unit with a header and a body.

#### **The Page Header**

Before the actual data, each page has a small header containing vital information for the reader:

| Header Field | Description |
| :---- | :---- |
| type | The type of page: DATA\_PAGE, DATA\_PAGE\_V2, DICTIONARY\_PAGE, or INDEX\_PAGE. |
| uncompressed\_page\_size | The size of the data after decompression. |
| compressed\_page\_size | The actual size of the data on disk. |
| crc | An optional checksum (CRC-32) to verify data integrity. |
| data\_page\_header | Specifics for a data page, including the encodings used for values, repetition, and definition levels. |
| dictionary\_page\_header | Information about the dictionary, such as the number of values and the encoding used. |

This header tells the reader exactly how to process the block of bytes that follows it.

#### **The Page Data**

Following the header, the actual page data is stored. For a data page, this is laid out with no padding or dividers in the following order:

1. **Repetition Levels (R-Levels)**: An encoded list of integers that indicates how repeated fields are structured. This is **only present for repeated fields**.  
2. **Definition Levels (D-Levels)**: An encoded list of integers that indicates how many optional fields in the data's schema path are present. This is how Parquet handles NULL or missing values efficiently. This is **present for any optional or repeated field**.  
3. **Encoded Values**: The actual data for the column, compressed and encoded according to the page header. For a simple column that is defined as REQUIRED, this is the *only* thing in the page data section.

---

### **The Magic of Nested Data: Definition and Repetition Levels**

One of Parquet's most powerful features is its ability to handle nested data structures (like a JSON object with arrays) efficiently. It achieves this using a technique from Google's Dremel paper involving **Definition Levels** and **Repetition Levels**.

These are two extra streams of metadata stored alongside the column values that allow Parquet to reconstruct complex, nested records from a flat, columnar format without wasting space.

Let's use a simple example schema:

message Contact {  
  required string name;  
  repeated group phone {  
    required string number;  
    optional string type;  
  }  
}

This defines a contact with a required name and a list of phone numbers. Each phone number has a required number and an optional type (e.g., "home", "work").

#### **Definition Levels (How deep is the data defined?)**

The definition level tells us how many optional or repeated fields in the path to a value are actually present for a given record.

* For the **phone.type** column:  
  * D=0: The entire phone group is missing for this contact.  
  * D=1: A phone entry exists, but its type is null.  
  * D=2: A phone entry exists, *and* its type is non-null.

By using definition levels, Parquet doesn't need to store null values at all. Their absence is encoded in this metadata.

#### **Repetition Levels (Where do lists begin and end?)**

The repetition level tells us when a new list item begins within a repeated field.

* For the **phone.type** column:  
  * R=0: This value is the first entry for a new contact.  
  * R=1: This value belongs to the same contact as the previous value.

#### **Example: Encoding Nested Data**

Let's see how this works for two contacts:

1. **Alice**: name: "Alice", phone: \[{number: "111", type: "home"}, {number: "222", type: null}\]  
2. **Bob**: name: "Bob", phone: \[\] (no phone numbers)

Here’s how the phone.type column data would be stored in Parquet:

| Encoded Value | Repetition Level (R) | Definition Level (D) | Belongs To | Notes |
| :---- | :---- | :---- | :---- | :---- |
| "home" | 0 | 2 | Alice | First phone for Alice; type is defined. |
| *null (not stored)* | 1 | 1 | Alice | Second phone for Alice; type is null. |
| *null (not stored)* | 0 | 0 | Bob | New record for Bob; phone group is empty. |

By reading the R and D levels, a query engine can perfectly reconstruct the original nested data for both Alice and Bob.

---

### **The Power of Indexes: ColumnIndex and OffsetIndex**

While the main footer tells a reader where to find the column chunks, Parquet has an additional, optional set of indexes for even faster data skipping. These indexes are written at the end of the file, just before the footer.

1. **ColumnIndex**: This structure stores statistics (min, max, null count) for *every page* within a column chunk. When a query has a filter (e.g., WHERE price \> 100), the query engine can first check the row group stats. If that passes, it can then check the ColumnIndex and skip reading individual pages within that chunk if their min/max range doesn't satisfy the filter. This provides a much more granular level of filtering.  
2. **OffsetIndex**: This acts as a table of contents for the pages within a column chunk. It stores the precise byte offset, length, and first row number for each page. This allows a reader to quickly seek to the exact start of a specific page without having to scan through the column chunk.

---

### **The Read Path: How a Query Engine Accesses Data**

So, how does a query engine like Spark or Presto actually read data from a Parquet file to reconstruct a specific row or a set of rows? It’s a multi-step process that avoids reading the entire file.

Let's say you want to execute the query SELECT product\_id, amount FROM sales WHERE category \= 'Electronics' AND amount \> 1000.

1. **Read the File Footer (The Map)**: The process always starts at the end of the file. The reader finds the last 4 bytes to confirm it’s a PAR1 file, then reads the 4 bytes before that to know the length of the footer metadata. It then jumps back and reads the entire **FileMetaData** structure into memory.  
2. **Column Pruning**: The engine analyzes the SELECT and WHERE clauses. It sees it only needs data from the product\_id, amount, and category columns. It will completely **ignore all other columns**, which immediately reduces the amount of data to be processed.  
3. **Predicate Pushdown (Row Group Level)**: The engine uses the WHERE clause and the statistics in the footer to eliminate entire Row Groups.  
   * If a row group's metadata shows its category values range from "Apparel" to "Groceries" and its max amount is 950.00, the engine knows it cannot possibly contain rows that satisfy category \= 'Electronics' AND amount \> 1000. **This entire row group is skipped.**  
   * If another row group's metadata shows its category range is "Electronics" to "Toys" and its amount range is 25.00 to 2500.00, the engine knows it **must read** from this group.  
4. **Reading and Decoding the Pages**: Now, the engine focuses only on the necessary column chunks within the necessary row groups.  
   * **Seek and Read**: Using the offsets from the footer, it seeks directly to the start of the category, amount, and product\_id column chunks.  
   * **Decode category Column**: It reads the first page, which is the **Dictionary Page**. It builds a small in-memory lookup table, for example: {0: "Clothing", 1: "Electronics", 2: "Toys"}. It then reads the **Data Pages**. The data is just a sequence of integers (e.g., 1, 1, 2, 0, 1...), and the engine uses the dictionary to translate these back to their string values ("Electronics", "Electronics", "Toys", "Clothing", "Electronics"...).  
   * **Decode amount Column**: It reads the data pages for the amount column. Let's say this data is stored using plain encoding and Snappy compression. The engine reads a compressed page into memory, decompresses it, and then has a block of floating-point numbers ready.  
   * **Decode product\_id Column**: Same as the amount column, the pages for product\_id are read and decompressed.  
5. **Row Reconstruction & Final Filtering**: The query engine now has three streams of decoded values, one for each required column. It reconstructs rows on-the-fly:  
   * It takes the 1st value from product\_id, the 1st from category, and the 1st from amount.  
   * It checks this reconstructed row against the WHERE clause: Is category "Electronics" and is amount \> 1000?  
   * If yes, the row is passed on to the next stage (or sent to the user).  
   * If no, the row is discarded.  
   * This process repeats until all values in the pages are consumed.

By doing this, the engine only materializes the rows that match the query, making the process incredibly memory-efficient and fast.

---

## **Updates and Deletes: The Immutability Challenge**

As mentioned, Parquet files are **immutable**. So how do you update or delete records? You don't—at least not directly. Instead, you create new files and use a transaction log to track the changes. This is where data lakehouse table formats come in.

* **Copy-on-Write (CoW)**: When a record is updated, the entire Parquet file containing that record is read, the change is applied in memory, and a completely new file is written without the old record or with the updated one. A transaction log is then updated to point to this new file and ignore the old one. This is great for read-heavy workloads but can be slow for frequent, small updates.  
* **Merge-on-Read (MoR)**: When a record is updated or deleted, the change is written to a smaller, separate "delta" file (e.g., a file that says "delete row with ID 123"). When you query the data, the engine reads both the original Parquet file and the delta file and merges them on the fly to produce the current state. This makes writes very fast but adds a slight overhead to reads.

These strategies, managed by systems like Delta Lake, Hudi, and Iceberg, give you the power of mutable data while retaining the performance and reliability benefits of immutable Parquet files.

### **Conclusion**

Apache Parquet is more than just a file format; it's a highly engineered solution for storing and querying massive datasets efficiently. Its columnar structure, combined with powerful compression, encoding, and metadata optimizations, makes it the ideal foundation for modern data warehousing and analytics. By understanding how it works under the hood—from its high-level row groups down to the individual pages and their encoding schemes—you can better appreciate why it has become an indispensable tool in the big data world.

